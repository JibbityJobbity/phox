# Phox

C++20 Vulkan 1.3 renderer. See README.md for features and app options, and docs/ for the architecture.

## Build environment

Everything comes from the nix flake; `.envrc` has `use flake`. Run commands inside it:
`nix develop -c bash -c '...'` (cd to absolute paths inside the command).

- **x86_64-linux** (`nixpkgs` input): gcc, Vulkan loader on `LD_LIBRARY_PATH`, validation layers, and
  lavapipe via `VK_DRIVER_FILES=$PHOX_LAVAPIPE_ICD`. Goldens are regenerated here, on lavapipe.
- **aarch64-darwin** (`nixpkgs-darwin` input, unstable): clang, MoltenVK (needs >= 1.3 for Vulkan 1.3),
  loader on `DYLD_LIBRARY_PATH` (SIP strips it when a system binary such as /bin/sh starts the program),
  validation layers. MoltenVK is found through `XDG_DATA_DIRS`; no lavapipe.
- CI (`.github/workflows`, Ubuntu 24.04) uses apt via `.github/actions/setup-phox`, not the flake.

Build directories (all git-ignored as `/build*`):

```bash
cmake -S standalone -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build-mac
cmake -S test -B build-mac-test -G Ninja && cmake --build build-mac-test
cd build-mac-test && PHOX_VALIDATION=1 PHOX_REQUIRE_GPU=1 ctest --output-on-failure
./build-mac/PhoxSample --debug --scene cubes              # headless; exit 3 means validation messages
./build-mac/PhoxSample --backend metal --debug --scene cubes   # the native Metal backend
```

On macOS ctest also runs `golden_metal` (the same goldens through the Metal backend).

(On Linux the README uses `build-standalone` / `build-test`.)

## Portability notes

- The instance enables `VK_KHR_portability_enumeration` when present, and the device enables
  `VK_KHR_portability_subset` when exposed (required by MoltenVK).
- Device selection requires descriptor indexing (the bindless texture table: runtime arrays, partial binding,
  update-after-bind, non-uniform indexing). lavapipe, desktop drivers and MoltenVK have it. It also
  requires `multiDrawIndirect` and `drawIndirectFirstInstance` (GPU-driven drawing); `drawIndirectCount` is
  optional because MoltenVK lacks it. `PHOX_NO_DRAW_INDIRECT_COUNT=1` tests the path without it.
- The Vulkan renderer keeps the scene on the GPU and uploads what changed: objects must be changed through
  their non-const accessors (which log them, `Scene::changes`), not through references kept across frames.
  The scene test compares a reused renderer against a fresh one after each kind of change.
- Shaders are checked by the validation layer's spirv-val: glslang emits an invalid `Aligned 0` for a
  whole-struct store through a buffer reference, so store such structs field by field.
- Goldens come from lavapipe: `VK_DRIVER_FILES=$PHOX_LAVAPIPE_ICD PHOX_UPDATE_GOLDEN=1
  ./build-test/PhoxGoldenTests` writes `<case>.ppm` from the vertex path and `<case>.lod.ppm` from the
  cluster LOD path (lavapipe has mesh shaders). Changes to `buildMeshletLod` change the `.lod.ppm` images. Shading changes (shaders/pbr.glsl) must be mirrored in core/Shading.cpp
  (software rasterizer) and shaders/metal/pbr.h.
- Assimp 5.4.3 is built with `ASSIMP_WARNINGS_AS_ERRORS OFF`; clang 21 warns in it.
- MoltenVK prints `[MVK-BUILD-ERROR] Cereal serialization ...` on exit; it is MoltenVK-internal and harmless.
- Metal backend (`source/phox/metal/*.mm`, ObjC++ with ARC, `PHOX_ENABLE_METAL`, on by default on Apple):
  shaders in `shaders/metal/` are compiled at runtime, because Xcode's offline `metal` compiler needs the
  Metal Toolchain component, which is not installed. MSL vertex shaders flip clip-space y (`toMetalClip`)
  since Phox's projection targets Vulkan. Metal API validation (`MTL_DEBUG_LAYER`) must be set before the
  process's first Metal device, which a Metal window creates; PhoxSample sets it for `--debug`.
