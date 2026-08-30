# Conventions

These exist because Phase 1 of the previous Peranti attempt found real C-vs-C++
compile failures when grafting plain C into Luanti's C++ tree. `extern "C"`
only changes *linkage* (name mangling) -- the code is still parsed by the
C++ front-end, which is stricter than C on several points below. Baking
these in from the start means the graft step later is a non-event instead
of a debugging session.

## 1. Every header AND its .c file get an extern "C" guard, inside the file itself

Not at the include site -- inside the file. This matters for two distinct
reasons depending on how the file ends up compiled:

- **Header**: any consumer, .c or .cpp, gets correct linkage on the
  *declarations* automatically.
- **.c file**: if this file is ever `#include`d raw into a C++ translation
  unit (the graft amalgamation, avoiding CMake edits), the header's own
  extern "C" block closes before the .c file's function *definitions*
  appear. Without a guard in the .c file too, those definitions get C++
  linkage while the header declared them with C linkage -- a hard mismatch,
  not a style nit. Guarding both means a file is fully self-contained: no
  includer, anywhere, ever has to remember to wrap it.

```c
// chunk.h
#ifndef PERANTI_CHUNK_H
#define PERANTI_CHUNK_H

#ifdef __cplusplus
extern "C" {
#endif

// ... declarations ...

#ifdef __cplusplus
}
#endif

#endif // PERANTI_CHUNK_H
```

```c
// chunk.c
#include "chunk.h"

#ifdef __cplusplus
extern "C" {
#endif

// ... definitions ...

#ifdef __cplusplus
}
#endif
```

## 2. No compound-literal address-of

C treats compound literals as lvalues; you can take their address. C++
(even as a GCC extension) treats them as temporaries -- taking their address
is a hard error, not a warning.

```c
// WRONG -- compiles in C, fails in C++
bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
    .data = SG_RANGE(cube_vertices),
});

// RIGHT -- named local, valid in both
sg_buffer_desc vbuf_desc = {0};
vbuf_desc.data = SG_RANGE(cube_vertices);
bind.vertex_buffers[0] = sg_make_buffer(&vbuf_desc);
```

## 3. Always cast malloc/calloc/realloc

C allows the implicit `void*` -> `T*` conversion. C++ requires an explicit
cast. The cast is a no-op under C, so there's no reason not to always
include it.

```c
// WRONG -- compiles in C, fails in C++
Chunk *chunks = calloc(count, sizeof(Chunk));

// RIGHT
Chunk *chunks = (Chunk *)calloc(count, sizeof(Chunk));
```

## 4. sokol never appears outside src/render/

`src/core/` (mat4, chunk, chunk_mesh, world_storage, camera) must compile
and link with zero sokol headers included, anywhere in the include chain.
This means:
  - core/ logic is unit-testable without a GPU context at all.
  - The two dangerous idioms above can only ever occur in one small
    directory (render/), not scattered across the whole codebase --
    `compat_lint.sh` only needs to check render/ (and later net/, if
    revived) as a result.
  - core/ data structures (Chunk, WorldChunkEntry, ChunkMeshData) are the
    contract between core/ and render/; render/ converts them into sg_*
    buffers, core/ never knows sg_* types exist.

## 5. sokol_app/sokol_glue never appear outside src/standalone/

`src/render/sokol_impl_luanti.c` is the graft target and must never include
sokol_app.h or sokol_glue.h -- Irrlicht (or whatever windowing owns the GL
context when grafted) is the sole owner of the window/context/event loop.
`src/standalone/sokol_impl_standalone.c` is the only file allowed to pull
those in, for the dev-only test harness in `src/standalone/main.c`. Nothing
in render/ or core/ may depend on anything standalone/ provides.

## 6. Undef sokol's *_IMPL macros immediately after use

sokol_gfx.h's implementation block is guarded only by `#ifdef SOKOL_GFX_IMPL`,
not a separate "already emitted" latch the way its declarations are
(`SOKOL_GFX_INCLUDED`). If `SOKOL_GFX_IMPL` stays defined for the rest of
the translation unit, any later `#include "sokol_gfx.h"` elsewhere (e.g. a
header that includes it just for declarations) silently re-emits the whole
implementation a second time -> redefinition errors. Always `#undef` the
`*_IMPL` macros right after the include that sets them.

## 7. Designated initializers: field-declaration order only, or avoid them

C permits designated initializers in any order; C++20 requires
declaration order. Since this project may eventually build under stricter
C++ standards levels, prefer named-local + field-assignment (rule #2's
"RIGHT" example) over designated-initializer struct literals for anything
that crosses the render/ boundary, and if using them at all, keep field
order matching the struct's declaration order.

## 8. Prefer memset over `= {0}` for sokol structs specifically

`= {0}` is safe in C++ when a struct's own first field (recursively,
through any nested struct) is a plain integer type -- `sg_desc` and
`sg_pass` both start with `uint32_t _start_canary`, so `= {0}` is fine for
those. But `sg_pass_action`'s first field is `colors[0].load_action`, an
*enum* with no leading canary to absorb the literal `0` -- and C++
disallows the implicit int-to-enum conversion this requires inside a
brace-initializer, where C allows it freely. This was found by the actual
C++ graft dry run failing on code that compiled fine as plain C -- worse,
the same code would have been a *silent semantic bug* even under C, since
`{0}`'s zero-value resolves to `_SG_LOADACTION_DEFAULT`, which sokol's own
docs confirm means `SG_LOADACTION_CLEAR`, not "leave existing content
alone."

Rather than case-by-case verifying which sokol structs happen to be safe,
default to `memset(&thing, 0, sizeof(thing));` for any sokol struct you're
zero-filling, then set the fields you actually care about explicitly by
name. `memset` is a byte-level operation, not a typed aggregate
initializer, so it never hits this restriction regardless of what type
the struct's first field happens to be.

```c
// RISKY -- fine for some sokol structs, a hard C++ error for others,
// and can silently pick the wrong "default" value even under C
sg_pass_action pass_action = {0};

// SAFE -- works for every sokol struct, no need to know its field order
sg_pass_action pass_action;
memset(&pass_action, 0, sizeof(pass_action));
pass_action.colors[0].load_action = SG_LOADACTION_LOAD;
```

## Enforcement

Run `./compat_lint.sh` before committing. It greps `src/render/` (and
`src/net/` if revived) for the two banned idioms. `src/core/` doesn't need
scanning by construction, since it can't reference sokol types to produce
a `sg_*` compound literal in the first place -- but the malloc/calloc cast
rule still applies there too, since core/ gets grafted into Luanti's C++
tree just as directly as render/ does.
