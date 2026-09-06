CC       := gcc
CXX      := g++
CSTD     := -std=c23
CXXSTD   := -std=c++17   # only used for VMA's implementation TU; VMA doesn't need anything newer

BUILD    := build
TARGET   := $(BUILD)/vulkanapp

# All real project logic stays plain C. Vendored third-party C sources (e.g. volk) go here too.
C_SRC    := src/main.c src/app.c src/third_party/volk/volk.c
# The ONLY C++ in the project: a one-liner that instantiates VMA's implementation.
CXX_SRC  := src/vma_impl.cpp

C_OBJ    := $(patsubst src/%.c,$(BUILD)/%.o,$(C_SRC))
CXX_OBJ  := $(patsubst src/%.cpp,$(BUILD)/%.o,$(CXX_SRC))
OBJ      := $(C_OBJ) $(CXX_OBJ)
DEP      := $(OBJ:.o=.d)

# --- Shaders: compiled to SPIR-V and embedded as C arrays at build time ---
# No runtime shader compilation, no shaderc dependency in the shipped binary,
# no .spv asset files to ship alongside it either.
SHADER_SRC_DIR   := shaders
SHADER_BUILD_DIR := $(BUILD)/shaders

SHADER_VERT := $(wildcard $(SHADER_SRC_DIR)/*.vert)
SHADER_FRAG := $(wildcard $(SHADER_SRC_DIR)/*.frag)
SHADER_INC  := $(patsubst $(SHADER_SRC_DIR)/%.vert,$(SHADER_BUILD_DIR)/%.vert.spv.inc,$(SHADER_VERT)) \
               $(patsubst $(SHADER_SRC_DIR)/%.frag,$(SHADER_BUILD_DIR)/%.frag.spv.inc,$(SHADER_FRAG))

$(SHADER_BUILD_DIR)/%.vert.spv.inc: $(SHADER_SRC_DIR)/%.vert
	@mkdir -p $(dir $@)
	glslc -mfmt=c $< -o $@

$(SHADER_BUILD_DIR)/%.frag.spv.inc: $(SHADER_SRC_DIR)/%.frag
	@mkdir -p $(dir $@)
	glslc -mfmt=c $< -o $@

# Anything with a .pc file goes through pkg-config
PKGS       := sdl3 vulkan
PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS))
PKG_LIBS   := $(shell pkg-config --libs $(PKGS))

# -I$(BUILD) so `#include "shaders/shader.vert.spv.inc"` resolves against the generated files.
CFLAGS   := $(CSTD)   -Wall -Wextra -Isrc -Isrc/third_party -Isrc/third_party/cglm/include -I$(BUILD) -MMD -MP $(PKG_CFLAGS) -g
CXXFLAGS := $(CXXSTD)               -Isrc -Isrc/third_party -Isrc/third_party/cglm/include -I$(BUILD) -MMD -MP $(PKG_CFLAGS)

# volk is vendored/compiled directly (see C_SRC). shaderc_combined is gone too, now that
# shaders are compiled at build time via glslc and embedded, rather than compiled at runtime.
# cglm's pkg-config support (cglm.pc) is inconsistent across distros/versions, so linked
# directly rather than through pkg-config. -lm is explicit because some cglm builds don't
# pull it in themselves even though the library needs it (sqrtf/sinf/etc).
# cglm is vendored header-only (src/third_party/cglm) — no library to link, just -lm
# for the libm calls (sqrtf, sinf, etc.) its inline functions make internally.
LDLIBS   := $(PKG_LIBS) -lm -lpthread -ldl

.PHONY: all debug clean

all: $(TARGET)

# Link with g++ (not gcc) so libstdc++ gets pulled in automatically for the VMA object.
$(TARGET): $(OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJ) -o $@ $(LDLIBS)

# $(dir $@) creates the exact nested directory each object needs (e.g. build/third_party/volk/)
# instead of relying on a single top-level $(BUILD) prerequisite, which broke on vendored subfolders.
#
# Order-only prerequisite on $(SHADER_INC): the .spv.inc files must exist before the first
# compile (or #include fails outright). Once they exist, gcc's -MMD already tracks them as
# real dependencies in the generated .d files, so edits to a .vert/.frag correctly trigger
# a shader rebuild *and* a recompile of whatever .c #includes it.
$(BUILD)/%.o: src/%.c | $(SHADER_INC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.cpp | $(SHADER_INC)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

debug: CFLAGS   += -g -O0 -DDEBUG
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: $(TARGET)

-include $(DEP)

clean:
	rm -rf $(BUILD)
