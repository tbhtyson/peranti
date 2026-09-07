# Peranti Roadmap: Vulkan Core + Luanti-Grafted Networking

## Starting point (as of this zip)

`src/main.c` is a monolithic SDL3 + volk + VMA + cglm prototype: one cube,
one pipeline, a fly camera. No chunk data, no meshing, no networking. This
roadmap treats that as Phase 0 and builds outward from it, reusing your
existing conventions (one job per file, hard fail at detection, fixed
capacity over dynamic, enums over defines).

## Strategic reversal from the sokol graft

Earlier work injected Peranti *into* Luanti's Irrlicht-based `.exe` via the
`all.c` bare-`#include` amalgamation trick, so Irrlicht kept driving the
window/render loop and Peranti's sokol calls rode along inside it.

That model doesn't fit this goal. You want your own Vulkan core to *be* the
engine, and only want Luanti's C++ for the parts that are painful to
reimplement correctly (wire protocol, SRP-6a auth, mapblock
serialization/versioning, content/item definition sync, formspecs, mod
channels). So the dependency direction flips:

- **Old graft:** Luanti owns `main()`, Peranti's `.c` files ride inside it.
- **New graft:** Peranti owns `main()` (your Vulkan loop), and a static lib
  built from a subset of Luanti's C++ (`network/`, `mapblock.cpp`,
  `nodedef.cpp`, `content_mapnode.cpp`'s serialization bits, `clientobject`
  sync) is linked in and called through a thin `extern "C"` shim.

You already have a from-scratch C networking stack (UDP framing, reliability
layer, SRP-6a, big-endian serialization) that works against a live server.
Two honest paths here, pick one explicitly rather than drifting:

1. **Keep your C networking, drop the Luanti-C++-networking idea.** Your
   layer already works. The "painful stuff" that's actually still painful
   is content definition sync (dynamic node/item defs, texture atlases,
   sounds), formspec parsing, and protocol version skew across Luanti
   releases — those are the parts worth grafting, not the UDP/SRP layer
   you've already solved.
2. **Replace your C networking with linked Luanti C++**, accepting a C++
   toolchain dependency and an `extern "C"` boundary, in exchange for
   automatic protocol-version compatibility as upstream Luanti evolves.

Given the "one job per file / explicit over clever" philosophy and that your
C stack already authenticates against a live server, (1) is the better
default: graft only content-definition sync and formspec/HUD packet
handling from Luanti C++, keep your own transport. Flag this as a decision
to confirm before Phase 6 — it changes how much of Luanti's build tree you
need to vendor.

---

## Phase 0 — Bifurcate the monolith

Not a five-way decomposition. `main.c` splits into exactly two files along
its natural seam:

- `src/init.c` — everything that runs once: instance/device/queue setup,
  swapchain creation, pipeline/shader module loading, the base cube's
  buffers.
- `src/loop.c` — everything that runs per-frame: input polling, camera
  update, command buffer recording, present, `recreateSwapchain` on
  resize/out-of-date.

The base cube stays whole and working in these two files as the reference
case. Every feature from Phase 1 onward lands in its own new file (e.g.
`chunk.c`, `mesh.c`, `cull.c`), and gets wired into `init.c`/`loop.c` behind
an explicit marker comment — something like `/* --- past base cube: chunk
streaming --- */` at the call site — so it's always obvious by grepping
`init.c`/`loop.c` which lines are the original prototype and which are
additions, without needing to open every new file to find out.

## Phase 1 — Luanti-compatible world representation

- Node struct: content id (u16) + param1 + param2, matching Luanti's node
  layout so mapblocks decode without a translation layer.
- Mapblock: 16³ nodes, matching Luanti's block size, decoded from the same
  zstd-framed, versioned format you already reverse-engineered (leading
  `0x1d` byte, version 29+, `ZSTD_decompress` with a known-size buffer since
  `ZSTD_getFrameContentSize` returns UNKNOWN for these frames).
- World storage: `ChunkCoord`-keyed hash map (open addressing, fixed
  capacity per your "no growable structures" rule — size it to your max
  view-distance chunk count and hard-fail on overflow rather than
  rehashing).
- `CONTENT_IGNORE` and `CONTENT_AIR` both treated as non-solid for meshing.

## Phase 2 — Binary greedy meshing v2 (Ethan Gore / cgerikj reference)

This is the actual "Ethan Gore" ask — his contribution to
`cgerikj/binary-greedy-meshing` v2 is specifically the bitwise face-culling
and vertex-pulling renderer:

- Per-axis, per-column 64-bit occupancy masks (62×62 interior + 1-node
  border sampled from neighbor mapblocks, so cross-chunk faces cull
  correctly without a stitching pass).
- Face visibility via mask XOR against a shifted copy of itself (bit set
  where solid meets non-solid).
- Greedy merge over the 2D face masks per axis using bitwise AND/shift to
  find run lengths, checking voxel-type equality via the original id array.
- Output format: 8 bytes/quad — 6 bits each for x, y, z, width, height (fits
  in 4 bytes) + voxel type in the low byte of the second 4 — no separate
  vertex buffer, this *is* the storage-buffer payload for vertex pulling.
- Run this off the main thread: one mesh job per dirty mapblock, feeding a
  ring of staging buffers.

## Phase 3 — Vertex pulling + indirect multidraw

- One big `VkBuffer` (VMA-suballocated) holding every chunk's packed quads;
  each chunk owns a `{offset, count}` range.
- Vertex shader has no vertex input at all — `gl_VertexIndex` decodes which
  of the 6 corners (`{0,1,2,2,3,0}` pattern, no index buffer) of which quad,
  and pulls the packed quad from the SSBO by `gl_InstanceIndex` or a
  push-constant chunk-base offset.
- `vkCmdDrawIndirect` / `vkCmdDrawIndexedIndirect` with one indirect-command
  buffer covering all visible chunks — check `multiDrawIndirect` device
  feature support; fall back to per-chunk `vkCmdDraw` calls on hardware that
  lacks it (rare on desktop, worth guarding since you already have a "no
  silent fallback" rule — log which path is active, don't fail silently).
- Coordinates: to eventually approach Gore's "renders full 32-bit range"
  claim, use camera-relative floating origin in the vertex shader (subtract
  camera chunk-base on CPU, keep intra-chunk offsets in the packed quad) —
  this also happens to fix the exact offset-subtraction bug you already
  identified in `peranti_camera_sync` on the graft track, just generalized
  beyond Luanti's ~62000-node map limit for future non-Luanti worlds.

## Phase 4 — GPU-driven frustum culling

- Compute shader, one thread per chunk: Gribb-Hartmann 6-plane test against
  each chunk's AABB (you already have this extraction method noted).
- Writes surviving chunks' indirect-draw commands into the buffer Phase 3
  consumes, with an atomic counter for count.
- `vkCmdDrawIndirectCount` if the device supports it (avoids CPU readback of
  the count); otherwise dispatch a fixed max-chunk-count `vkCmdDrawIndirect`
  and zero out non-visible entries' `instanceCount` in the compute shader —
  this keeps you on the "fixed capacity, no dynamic" convention instead of
  needing an indirect-count extension everywhere.
- macOS/MoltenVK note from your sokol days (no compute passes in GLCORE)
  doesn't apply here — MoltenVK supports compute — but confirm
  `multiDrawIndirect`/`drawIndirectCount` support on MoltenVK specifically
  before depending on it; keep the CPU-culling fallback path from your
  compute shader as a `#if` rather than deleting it.

## Phase 5 — Streaming & multithreading (only after profiling)

Matches your already-agreed order for the standalone client:

1. Camera-driven chunk load/unload against the sparse `ChunkCoord` map.
2. Worker thread pool for decode+mesh jobs (thread count = profiled
   bottleneck, not guessed).
3. Re-profile at real streaming scale before adding more threads — you
   already have `sg_apply_bindings`/fill-rate profiling discipline from the
   graft track; carry that habit here rather than threading preemptively.

## Phase 6 — Graft the Luanti pieces you actually decided to keep

Once Phase 0's decision is made:

- Vendor only the needed Luanti C++ TUs (content/item def manager,
  mapblock version-decode helpers if you want to stop hand-maintaining
  that, formspec parser) into a static lib target, separate from Irrlicht
  and separate from Luanti's own `main()`.
- `extern "C"` shim header, same discipline as `all.c`'s dual-guard
  requirement (both `.h` and `.c`/`.cpp` sides) so your C engine can call
  into it without a C++ ABI leak.
- This is a much smaller vendoring surface than the full graft was, since
  you're not fighting Irrlicht's global GL state anymore — no
  `PerantiGLState` save/restore bracket needed, because there's no second
  renderer sharing the context.

## Phase 7 — Collision, then raytracing

- Collision first, against raw node data (not the greedy-merged mesh) —
  AABB-vs-voxel sweep using the same coordinate space as Phase 3's
  camera-relative offsets.
- Raytracing: Gore's own finding (raster primary rays, RT/compute-raymarch
  for shadows and GI) is the right default rather than full hardware RT
  BVH-per-chunk, since your data is already grid-aligned — a brick-based DDA
  raymarch compute pass against the same occupancy data from Phase 2 is
  simpler than building/updating a BLAS per dirty chunk. Revisit hardware
  `VK_KHR_ray_tracing` only if the raymarch profiles worse than expected.

## Phase 8 — Modding surface (long horizon, unchanged)

Lua API surface for mod-games like VoxeLibre, contingent on 0–7 being
solid — no reason to design this before the engine can hold a real world.

---

## Luanti edge cases beyond voxel cube geometry

These fall outside the binary-greedy-mesh happy path and need explicit
handling in Phase 1 (world representation) or later grafted phases.

**Node geometry**
- `drawtype` other than `normal`: nodebox (stairs, slabs, fences — arbitrary
  AABB unions), mesh nodes (custom models), plantlike (crossed quads:
  flowers/grass), liquid/flowingliquid (corner-height interpolated flow
  shape, animated), glasslike/glasslike_framed (neighbor-connected,
  partial-transparent), torchlike, signlike, firelike, railslike, allfaces
  (leaves — different culling than solid cubes).
- Only `drawtype == normal` nodes belong in the greedy-mesh occupancy mask;
  everything else needs its own small mesh generator and its own
  opaque/non-opaque flag for occluding the greedy-meshed neighbors
  correctly.
- `param2` is polymorphic based on `paramtype2`: facedir/4dir (rotation),
  wallmounted, leveled (snow-layer height fraction), color, degrotate,
  meshoptions. Can't be treated as a flat byte once rotation/nodeboxes are
  in play.

**Lighting**
- `param1` packs day/night light banks. This is a separate propagation
  system (sunlight column cast + BFS spread from artificial sources,
  recomputed on edit), not derivable from occupancy alone. Needed before
  anything looks like more than flat-shaded blocks.

**Content definition ordering**
- Content ids are server-assigned and dynamic. A mapblock can reference an
  id before its `NODEDEF` packet has arrived — need an explicit "unknown
  node" placeholder mesh plus a re-mesh-on-def-arrival path, not an
  assumption of in-order arrival.

**Node metadata & timers**
- Chests/furnaces/signs carry metadata (inventories, text) sent separately
  from static mapblock bytes (`NodeMetadataList`); some nodes have
  per-node timers (furnace cooking). This is extra state to hang off
  `ChunkCoord` map entries, not part of the raw node array.

**Entities / Active Objects**
- Dropped items, mobs, and other players arrive as `GenericCAO` packet
  streams — a second renderer path (bone-based skeletal animation for
  meshed entities, attachment system composing entity/player/bone
  transforms) entirely separate from the voxel quad-pulling pipeline.

**Player/physics parity**
- Server-pushed `physics_override` (speed, jump, gravity, sneak, liquid
  viscosity, new-move toggle) changes local movement at runtime. Ladders,
  climbable nodes, and sneak-ledge behavior are specific enough that
  approximate collision will visibly desync from vanilla clients on shared
  servers.

**UI surface**
- Formspecs: a separate string-based UI description language for
  inventories/chests/HUDs, re-parsed per open/update. HUD elements
  (statbars, waypoints, images, minimap) are a distinct packet-driven
  overlay on top. Large and intricate enough to be the strongest candidate
  for the actual Luanti-C++ graft from Phase 6, since the parser logic
  rarely changes upstream and is not worth hand-reimplementing.

**Protocol gotchas**
- Sounds and particles are independent server-triggered packet types, not
  tied to node data directly. Skybox/sun/moon/star params are
  server-configurable. Protocol version negotiation in
  `TOSERVER_INIT`/`SERVER_HELLO` can reject the client outright on version
  drift.

## Open decisions to confirm before committing code

1. Keep your C networking stack, or replace with linked Luanti C++
   (recommendation above: keep it, graft only content-def sync).
2. Target `multiDrawIndirect`/`drawIndirectCount` as required device
   features, or always keep the CPU-culling fallback path live.
3. Whether Phase 7 raytracing targets desktop-only (hardware RT viable) or
   must also run on the same GLCORE-class fallback hardware your sokol
   track cared about (raymarch-only, no hardware RT path at all).
