# Phox

A small C++20 renderer with a Vulkan 1.3 backend, built around a render graph. It draws a `Scene`
(meshes, materials, transforms, a camera and a directional light) either to a window or headless into an
image, and falls back to a software rasterizer when there is no usable GPU.

- **Render graph**: passes declare what they read and write; the graph culls unused work, derives every
  barrier (synchronization2) and manages transient images and buffers. See [Render graph](#render-graph).
- **Frames in flight**: two frame contexts with their own command buffers, fences, upload memory and
  descriptor pools. The CPU never waits for the device inside a frame.
- **Forward rendering into HDR** with depth testing, then tone mapping (clamp, Reinhard or ACES) straight
  into the swapchain image. Pipelines come from a cache keyed by a backend-free `PipelineDesc`, and use
  dynamic rendering.
- **Verified output**: golden images rendered on lavapipe, checked with the Khronos validation layer
  (including synchronization validation) on every CI run.

## Build and run

The nix flake provides the toolchain, Vulkan loader and headers, validation layers, a CPU Vulkan driver
(lavapipe), SDL2, glm and Graphviz: `nix develop` (or `direnv allow`).

```bash
cmake -S standalone -B build-standalone
cmake --build build-standalone
./build-standalone/PhoxSample                               # one frame of the triangle -> build/rendered_triangle.ppm
./build-standalone/PhoxSample --scene cubes --window        # animated, until the window is closed
./build-standalone/PhoxSample --mesh model.obj --window     # a model file on a turntable
./build-standalone/PhoxSample --help
```

Useful options:

| Option | Effect |
| --- | --- |
| `--scene triangle\|cubes` | the sample scene to show |
| `--window`, `--frames N` | present to a window (for N frames) instead of rendering one image |
| `--time S`, `--size WxH`, `--output FILE` | headless: animation time, image size, output path |
| `--tonemap clamp\|reinhard\|aces`, `--exposure X` | tone mapping |
| `--debug` | validation layer and debug logging; exits with status 3 if validation reported anything |
| `--device NAME` | use the GPU whose name contains NAME (also `PHOX_DEVICE`) |
| `--dump-graph FILE` | write the frame's render graph as Graphviz DOT (`dot -Tsvg FILE -o graph.svg`) |

To run on lavapipe instead of your GPU, as CI does: `VK_DRIVER_FILES=$PHOX_LAVAPIPE_ICD ./build-standalone/PhoxSample`.
Logging goes to stderr; `PHOX_LOG=debug|info|warn|error|off` sets the level.

## Tests

```bash
cmake -S test -B build-test && cmake --build build-test && ctest --test-dir build-test
```

| Test | What it checks | GPU |
| --- | --- | --- |
| `render_graph` | culling, versions, barriers, lifetimes, attachments, validation of declarations, DOT export | no |
| `render_view` | projection, camera, transforms, meshes, frustum culling, sorting, software rasterizer, OBJ loading | no |
| `scene_smoke` | the sample scenes and one headless frame (GPU or software fallback) | optional |
| `golden` | renders the sample scenes and compares them with `test/golden/*.ppm`; the software rasterizer must match them too | yes (skips without one) |

The golden test reads a few environment variables:

- `PHOX_VALIDATION=1` runs with the validation layer and fails on any message.
- `PHOX_REQUIRE_GPU=1` fails instead of skipping when no Vulkan device is available.
- `PHOX_UPDATE_GOLDEN=1` (or a comma-separated list of case names) rewrites the goldens. Regenerate them on
  lavapipe, which is deterministic: `VK_DRIVER_FILES=$PHOX_LAVAPIPE_ICD PHOX_UPDATE_GOLDEN=1 ./build-test/PhoxGoldenTests`.
  Mismatching images are written to `golden_out/` in the build directory.

CI (`.github/workflows`) builds on Ubuntu 24.04, runs every test on lavapipe with validation, and renders
the sample scenes with the standalone app.

## How a frame is made

```
Scene ──extractView()──▶ RenderView ──feature passes──▶ RenderGraph ──compile──▶ vk::GraphExecutor ──▶ GPU
 (data)                  (matrices,     addForwardPass     (backend-free)            (barriers, dynamic
                          culled,       addTonemapPass                                rendering, pooled
                          sorted draws) addReadbackPass                               transients)
```

| Path | Contents |
| --- | --- |
| `include/phox/` | backend-free API: `Scene`, `Camera`, `RenderView`, `RenderGraph`, `Resources`, `Renderer`, `Window`, `Math`, `Log` |
| `include/phox/pipeline/` | `PipelineDesc` and the shader/pipeline base types |
| `include/phox/vulkan/` | the Vulkan backend: `Renderer`, `Device`, `FrameContext`, `GraphExecutor`, `TransientPool`, `PipelineCache`, `MeshCache`, ... |
| `include/phox/vulkan/passes/` | feature passes that add themselves to a frame's graph |
| `source/phox/` | library sources (`core/`, `vulkan/`, `platform/`) |
| `shaders/` | GLSL, compiled to SPIR-V by CMake; `common.glsl` mirrors `vulkan/ShaderInterface.hpp` |
| `standalone/` | the `PhoxSample` app |
| `test/` | tests and golden images |
| `docs/architecture.md` | the design and its rationale |

## Render graph

The graph is rebuilt every frame from cached objects. A pass declares how it uses each resource; writes
return a new version of the resource, which later passes must use:

```cpp
RenderGraph graph(extent);
const ImageHandle hdr = graph.createImage("hdr", {RelativeExtent{1.0f}, Format::RGBA16Float});
const ImageHandle depth = graph.createImage("depth", {RelativeExtent{1.0f}, Format::D32Float});

struct ForwardOut { ImageHandle color; };
const ForwardOut &forward = graph.addPass<ForwardOut>(
    "forward", PassType::Graphics,
    [&](PassBuilder &b, ForwardOut &out) {
        out.color = b.colorAttachment(hdr, LoadOp::Clear);
        b.depthAttachment(depth, LoadOp::Clear, StoreOp::DontCare);
    },
    [&](const ForwardOut &, PassContext &context) {
        auto &ctx = vk::PassContext::from(context);  // command buffer, images, frame memory
        // bind and draw; rendering was begun from the declared attachments
    });

graph.addPass("tonemap", PassType::Graphics,
    [&](PassBuilder &b) {
        b.read(forward.color, ImageUsage::Sampled);
        b.colorAttachment(backbuffer, LoadOp::DontCare);   // an imported swapchain image
    },
    /* ... */);
graph.compile();
graph.execute(executor);
```

- Passes run in the order added. A pass is kept if it has `keep()`, writes the final version of an
  imported resource, or produces something a kept pass reads.
- Barriers come from how usages change between passes (and write hazards), batched per pass.
- Transient resources live from their first to their last use and come from a pool, so steady-state
  frames create nothing.
- Invalid declarations (reading before anything writes, loading undefined contents, stale versions,
  attachments in compute passes, ...) throw when the pass is added.

Derived from [ModernCppStarter](https://github.com/TheLartians/ModernCppStarter).
