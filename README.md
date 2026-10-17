# Phox

A small C++20 renderer with a Vulkan backend. It renders a `Scene` offscreen and writes the
result to `build/rendered_triangle.ppm`. If Vulkan is unavailable it falls back to a software
rasterizer.

## Layout

| Path | Contents |
| --- | --- |
| `include/phox/*.hpp` | Backend-agnostic interfaces (`Renderer`, `Device`, `Buffer`, `Scene`, ...) |
| `include/phox/vulkan/` | Vulkan implementations (`phox::vk::*`) |
| `source/phox/` | Library sources (`core/`, `vulkan/`, `platform/`) |
| `shaders/` | GLSL, compiled to SPIR-V by CMake (needs `glslangValidator`) |
| `standalone/` | Example executable |
| `test/` | Scene smoke test (registered with CTest) |

## Build

The nix flake provides the toolchain, Vulkan, SDL2 and glm: `nix develop` (or `direnv allow`).

```bash
cmake -S standalone -B build-standalone
cmake --build build-standalone
cd /some/scratch/dir && /path/to/build-standalone/PhoxSample   # writes ./build/rendered_triangle.ppm
```

Tests:

```bash
cmake -S test -B build-test && cmake --build build-test && ctest --test-dir build-test
```

Shader paths are baked in at build time (`PHOX_SHADER_DIR`), so the binary can run from any directory.

Derived from [ModernCppStarter](https://github.com/TheLartians/ModernCppStarter).
