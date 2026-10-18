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
cd /some/scratch/dir && /path/to/build-standalone/PhoxSample   # headless: writes ./build/rendered_triangle.ppm
/path/to/build-standalone/PhoxSample --window                  # open an SDL window and present until closed
/path/to/build-standalone/PhoxSample --window --frames 120     # ...or for N frames
```

Windowed rendering needs SDL2 (provided by the flake). Frames are rendered offscreen and blitted to the
swapchain, so the pipeline does not depend on the surface format. The window is optional:
`Renderer::initialize(debug, window)` takes a `phox::Window*` that must outlive the renderer.

Tests:

```bash
cmake -S test -B build-test && cmake --build build-test && ctest --test-dir build-test
```

Shader paths are baked in at build time (`PHOX_SHADER_DIR`), so the binary can run from any directory.

## Render graph

`phox/RenderGraph.hpp` is a small backend-agnostic frame graph. Each frame, `vk::Renderer` declares its passes and
the images they touch; the graph does the bookkeeping:

```
scene --writes--> color (transient) --read--> present (blit to swapchain)   [windowed]
                                      --read--> readback (copy to staging)  [headless]
```

- passes run in the order added; passes whose results nothing consumes are culled
- layout transitions and barriers are derived from how each pass uses an image (`ImageUsage`), including
  write-after-write hazards, instead of being written by hand
- transient images are allocated at first use and live until the last pass that uses them
- `vk::GraphExecutor` is the Vulkan backend: it records the whole frame into one command buffer

Scope today: images only (buffers, e.g. the vertex/uniform buffers, are still managed per frame outside the graph),
no resource aliasing, no pass reordering. `test/source/render_graph_test.cpp` tests the graph logic without a GPU.

Derived from [ModernCppStarter](https://github.com/TheLartians/ModernCppStarter).
