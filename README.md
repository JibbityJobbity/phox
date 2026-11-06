# Phox

A small C++20 renderer with a Vulkan 1.3 backend and, on Apple platforms, a native Metal backend, built
around a render graph. It draws a `Scene`
(meshes, materials, transforms, a camera and a directional light) either to a window or headless into an
image, and falls back to a software rasterizer when there is no usable GPU.

- **Render graph**: passes declare what they read and write; the graph culls unused work, derives every
  barrier (synchronization2) and manages transient images and buffers. See [Render graph](#render-graph).
- **Frames in flight**: two frame contexts with their own command buffers, fences, upload memory and
  descriptor pools. The CPU never waits for the device inside a frame.
- **Physically based materials**: glTF 2.0's metallic-roughness model (GGX, height-correlated Smith,
  Schlick) with base colour, metallic-roughness, normal, occlusion and emissive textures, alpha masking and
  blending, in linear light. Textures are mipmapped on the CPU and, on Vulkan, indexed from one bindless
  texture table. See [architecture section 6.3](docs/architecture.md#63-materials-and-textures).
- **glTF 2.0 loading** (`.gltf` and `.glb`, via cgltf and stb_image): node hierarchies, shared meshes and
  textures, samplers, PNG and JPEG images.
- **Forward rendering into HDR** with depth testing, then tone mapping (clamp, Reinhard or ACES) and sRGB
  encoding straight into the swapchain image.
- **Mesh shading** where the device has `VK_EXT_mesh_shader`: meshes are split into meshlets with
  meshoptimizer, and a task shader skips meshlets outside the view frustum, facing away from the camera,
  or hidden behind what is already drawn (two-phase occlusion culling against a depth pyramid). Elsewhere
  the same images come from the vertex pipeline. See [architecture sections 6.1 and
  6.2](docs/architecture.md#61-mesh-shading). Pipelines come from a cache keyed by a backend-free `PipelineDesc`, and use
  dynamic rendering.
- **Cluster LOD** (as in Nanite): each mesh is built into a hierarchy of meshlets at successive levels of
  detail, simplified group by group with shared borders locked, and the task shader picks per part of the
  mesh the coarsest meshlets whose error stays under a pixel, without cracks. On the `field` benchmark that
  draws 1% of the triangles and cuts the forward passes from 0.75 ms to 0.20 ms. See [architecture section
  6.4](docs/architecture.md#64-cluster-lod).
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
./build-standalone/PhoxSample --scene materials --window    # the material model: metals, plastics, textures
./build-standalone/PhoxSample --mesh model.glb --window     # a glTF (or OBJ) file on a turntable
./build-standalone/PhoxSample --help
```

Useful options:

| Option | Effect |
| --- | --- |
| `--scene triangle\|cubes\|tori\|materials\|field` | the sample scene to show (`field` is a 37.7-million-triangle benchmark) |
| `--mesh FILE` | show a glTF 2.0 (`.gltf`, `.glb`) or OBJ file on a turntable instead |
| `--window`, `--frames N` | present to a window (for N frames) instead of rendering one image |
| `--frames N` (headless) | render N frames of one pose (`--animate`: advancing 1/60 s each) and print the average GPU time per pass |
| `--mesh-shaders on\|off\|noocclusion\|nocull` | draw with task and mesh shaders where available; `noocclusion` keeps frustum and cone culling only, `nocull` turns meshlet culling off |
| `--lod on\|off`, `--lod-error PIXELS` | cluster LOD with mesh shaders, and the error it may show (default 1 pixel) |
| `--stats` | print how many triangles cluster LOD selected out of the full detail |
| `--time S`, `--size WxH`, `--output FILE` | headless: animation time, image size, output path |
| `--tonemap clamp\|reinhard\|aces`, `--exposure X` | tone mapping |
| `--backend vulkan\|metal` | the GPU backend (Metal on Apple platforms only; Vulkan there runs on MoltenVK) |
| `--debug` | validation (Vulkan validation layer, or Metal API validation) and debug logging; exits with status 3 if validation reported anything |
| `--device NAME` | use the GPU whose name contains NAME (also `PHOX_DEVICE`) |
| `--async-compute on\|off` | run async compute passes on a separate compute queue where the GPU has one (default on) |
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
| `render_view` | projection, camera, transforms, meshes and tangents, frustum culling, sorting, meshlets and their culling tests, sphere projection, textures and mips, materials, blending and masking in the software rasterizer, OBJ and glTF loading | no |
| `scene_smoke` | the sample scenes, one headless frame (GPU or software fallback) and its GPU pass timings | optional |
| `golden` | renders the sample scenes with vertex shaders, with mesh shaders and occlusion culling, and with mesh shaders and frustum and cone culling only, and compares each with `test/golden/*.ppm`; the software rasterizer must match them too. Then with cluster LOD, against `test/golden/*.lod.ppm` | yes (skips without one) |
| `golden_metal` | the same goldens through the Metal backend, vertex shaders only (Apple platforms) | yes |

The golden test reads a few environment variables:

- `PHOX_VALIDATION=1` runs with the validation layer and fails on any message.
- `PHOX_REQUIRE_GPU=1` fails instead of skipping when no Vulkan device is available.
- `PHOX_REQUIRE_MESH_SHADERS=1` fails instead of skipping the mesh shader run when the device lacks them.
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
| `include/phox/metal/` | the Metal backend, the same shape as `vulkan/` (Objective-C++; `Renderer.hpp` is plain C++) |
| `source/phox/` | library sources (`core/`, `vulkan/`, `platform/`) |
| `shaders/` | GLSL, compiled to SPIR-V by CMake; `common.glsl` mirrors `phox/ShaderInterface.hpp` |
| `shaders/metal/` | the MSL counterparts, compiled by the Metal backend at startup |
| `standalone/` | the `PhoxSample` app, and `PhoxRhiSample` (`standalone/rhi/`, the RHI stack below) |
| `test/` | tests and golden images |
| `docs/architecture.md` | the design and its rationale |

### The RHI stack

A second, independent stack sits beside the renderer above. It was built from the original baseline and
has not been ported onto it (or it onto them) yet:

| Path | Contents |
| --- | --- |
| `include/phox/rhi/`, `source/phox/rhi/` | `PhoxCore`: RHI interfaces (handles, descs, capabilities, `CommandList`, `Device`), its own log sink (`phox/rhi/Log.hpp`) |
| `include/phox/graph/`, `source/phox/graph/` | `PhoxCore`: a render graph over the RHI (capability checks, barriers, transient pooling) |
| `backends/null/` | `PhoxNull`: records commands, draws nothing; for tests without a GPU |
| `backends/vulkan/` | `PhoxVulkan`: the RHI on Vulkan 1.3 (`phox::vulkan`, separate from the renderer's `phox::vk`) |
| `include/phox/scene/` | `PhoxScene`: the baseline scene types in `phox::scene` |
| `techniques/forward/` | `PhoxTechniques`: the forward technique built from graph components |

Its tests are `image_compare`, `rhi_null`, `graph`, `camera`, `forward_graph`, and on a GPU `rhi_triangle`
and `forward`. The last two compare exactly with `test/golden/triangle_640x480.ppm` from lavapipe; on
other hardware set `PHOX_GOLDEN_TOLERANCE` (2 is enough on an RTX 3080).

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
- Compute passes that call `b.async()` run on a separate compute queue when the device has one; the graph
  splits the frame into submissions and orders them with timeline semaphores. Elsewhere they run in order on
  the graphics queue. See [architecture section 3.6](docs/architecture.md#36-async-compute).
- Invalid declarations (reading before anything writes, loading undefined contents, stale versions,
  attachments in compute passes, ...) throw when the pass is added.

Derived from [ModernCppStarter](https://github.com/TheLartians/ModernCppStarter).
