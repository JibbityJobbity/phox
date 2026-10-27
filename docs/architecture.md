# Phox architecture

Status: implemented, 2026-10-28. This started as a plan written against commit `f746304`; the six milestones
it laid out are now in the history (`8d30082` to `60a92e0`). The document describes the design as built, why
it is shaped this way, and where the implementation departed from the plan. Section 11 lists what comes next.

## 1. Shape

```
  Scene (plain data: objects, meshes, materials, camera, light, tone mapping)
      |  extractView()                         core, no Vulkan, unit-tested
      v
  RenderView (matrices, light, culled and sorted DrawItems)
      |
      |  feature passes: addForwardPass / addTonemapPass / addReadbackPass
      |  each declares its passes and resources on the graph
      v
  RenderGraph (core, backend-free)
      compile: resolve sizes, cull, lifetimes, barriers, attachments
      |
      |  execute(GraphBackend&)
      v
  vk::GraphExecutor (per frame, owns nothing across frames)
      |
      +-- FrameContext (ring of 2): command pool, fence, acquire semaphore, UploadArena,
      |                             DescriptorAllocator, DeletionQueue
      +-- TransientPool: graph images and buffers, recycled across passes and frames
      +-- PipelineCache + ShaderLibrary: PipelineDesc -> VkPipeline; fixed set layouts
      +-- MeshCache: device-local mesh buffers, uploaded once per mesh version
      +-- vk::Device: the only place that calls VMA
```

Namespaces:

- `phox::` holds the scene, math, render graph, resource and pipeline descriptions, view extraction and the
  software rasterizer. Nothing in it includes a Vulkan header.
- `phox::vk::` holds everything that touches Vulkan, including the bodies of the passes.

Two rules keep the design small:

1. **The graph tracks resources whose state changes inside a frame**: transient targets, the swapchain image,
   the readback buffer. Resources that stay in one state once uploaded (mesh buffers, later textures) live in
   caches and get one barrier at upload.
2. **The graph is rebuilt every frame.** It is bookkeeping over cached objects (pipelines, pooled images,
   uploaded meshes), so rebuilding costs little and there is no frame-to-frame graph state to get wrong.

## 2. Decisions

| # | Decision | Choice | Why |
|---|---|---|---|
| 1 | Abstraction boundary | Descriptions and handles are backend-free; command recording is Vulkan-only, inside pass bodies | One backend exists. A portable command API is a project of its own |
| 2 | Rendering model | Vulkan 1.3: synchronization2 for barriers and submits, dynamic rendering | No render pass or framebuffer objects; matches a graph that does its own transitions |
| 3 | Graph handles | Versioned (index + version); execution order is declaration order | Exact culling and hazard tracking now; reordering later needs no API change |
| 4 | Attachments | Declared in pass setup; the backend begins and ends rendering | Pass bodies only bind and draw; the graph knows load, store and clear values |
| 5 | Transient memory | Pool keyed by description and usage, reusable as soon as released | Steady-state frames create nothing; aliasing can come later |
| 6 | Frames in flight | 2 | Enough to overlap CPU and GPU |
| 7 | Math | glm behind `phox/Math.hpp`, configured for Vulkan's [0, 1] depth | Quaternions, frustums and clip-space conventions are easy to get wrong by hand |
| 8 | Verification | Golden images rendered on lavapipe, checked under synchronization validation | Lavapipe is deterministic and runs the real code path in CI |

## 3. Render graph

`include/phox/RenderGraph.hpp`, `source/phox/core/RenderGraph.cpp`; tests in `test/source/render_graph_test.cpp`.

### 3.1 Resources and handles

Images and buffers, each either transient (`createImage`, `createBuffer`) or imported (`importImage`,
`importBuffer`, with an initial and a final usage). Handles are `{index, version}`. A write returns the next
version; a pass may only use the current one, so every read names exactly what produced it.

Image sizes are absolute (`Extent2D`) or `RelativeExtent{scale}` of the graph's output extent, which is given
to the constructor: `RenderGraph graph(extent)`.

### 3.2 Usages

`ImageUsage` and `BufferUsage` live in `phox/Resources.hpp`, shared with resource creation: a resource is
created with the union of its usages, as a mask of `usageBit()`.

| ImageUsage | Layout | Stage | Access |
|---|---|---|---|
| ColorAttachment | COLOR_ATTACHMENT_OPTIMAL | COLOR_ATTACHMENT_OUTPUT | COLOR_ATTACHMENT_READ \| WRITE |
| DepthAttachment | DEPTH_STENCIL_ATTACHMENT_OPTIMAL | EARLY \| LATE_FRAGMENT_TESTS | DEPTH_STENCIL_ATTACHMENT_READ \| WRITE |
| DepthReadOnly | DEPTH_STENCIL_READ_ONLY_OPTIMAL | fragment tests and the pass's shader stages | DEPTH_STENCIL_ATTACHMENT_READ \| SHADER_SAMPLED_READ |
| Sampled | SHADER_READ_ONLY_OPTIMAL | the pass's shader stages | SHADER_SAMPLED_READ |
| StorageRead / StorageWrite | GENERAL | the pass's shader stages | SHADER_STORAGE_READ / READ \| WRITE |
| TransferSrc / TransferDst | TRANSFER_SRC / DST_OPTIMAL | ALL_TRANSFER | TRANSFER_READ / WRITE |
| Present | PRESENT_SRC_KHR | NONE | NONE |

Shader stages come from the pass type (graphics: vertex and fragment; compute: compute), which each barrier
records for both sides. The source side keeps only write accesses. A state a resource entered the graph in
(and the first use of any transient) has the source scope ALL_COMMANDS / MEMORY_WRITE: whatever came before
on the queue, including a previous user of a pooled image or the semaphore wait for a swapchain image.

### 3.3 Declaring passes

```cpp
graph.addPass(name, PassType::Graphics, setup, execute);         // setup(PassBuilder&), execute(PassContext&)
const Data &d = graph.addPass<Data>(name, type, setup, execute); // setup(PassBuilder&, Data&),
                                                                 // execute(const Data&, PassContext&)
```

`PassBuilder` offers `read` and `write` for images and buffers, `colorAttachment`, `depthAttachment`,
`depthReadOnly` and `keep`. Rules, each a `std::logic_error` naming the pass and resource (unknown handles are
`std::out_of_range`):

- Reads need contents: a producer, or an import with a defined initial usage. So does `LoadOp::Load`.
- A pass may use each resource in one way only; repeated identical reads merge.
- Usages must suit the call: `read` takes read usages, `write` takes StorageWrite or TransferDst, and render
  targets go through the attachment calls. Storage and transfer writes keep the previous contents' producer
  alive, since they may cover part of the resource; Clear and DontCare attachments do not.
- Attachments only in graphics passes, colour formats as colour and depth formats as depth; a graphics pass
  needs at least one attachment.

These are checked as the pass is added, not in `compile()` as first planned: the information is all there,
and the error then points at the code that made it. A throwing `addPass` leaves the graph unchanged.

### 3.4 Compile and execute

`compile()` resolves sizes, culls (roots are kept passes and writers of an import's final version; a live
pass makes the producers of what it reads live), walks live passes to emit barriers when the usage changes or
either side writes, records first and last use, builds attachment lists and the render area (attachments in
one pass must match in size), and emits final barriers for imports. `firstUse(image)` reports how an import is
first touched; `toDot()` exports the compiled graph.

`execute()` drives a `GraphBackend`: for each live pass, acquire the transients first used there, issue its
barriers as one batch, `beginPass`, run the body, `endPass`, release the transients last used there; then
the final barriers.

### 3.5 The Vulkan executor

`vk::GraphExecutor` is created per frame. It takes transients from the `TransientPool`, issues one
`vkCmdPipelineBarrier2` per batch, begins dynamic rendering from the declared attachments (and sets a full
viewport and scissor, since every pipeline uses them as dynamic state), and labels passes when debug utils
are on. Pass bodies get a `vk::PassContext`: command buffer, `image`, `view`, `extent`, `buffer`, the device
and the frame context.

The pool hands a released resource out again immediately rather than after the frames in flight have
finished, as first planned. That is safe because all frames go to one queue and a transient's first use is a
barrier from ALL_COMMANDS / MEMORY_WRITE, which orders it after every earlier use. Resources idle for 60
frames are destroyed.

## 4. Frame orchestration

`vk::Renderer::render()`:

```
frame = frames[n % 2]; frame.begin()                 wait its fence; reset pool, arena, descriptors; run deferred deletions
transients.collect(n); meshes.collect(n)
acquire(swapchain, frame.imageAvailable)             out of date: recreate, skip the frame
view = extractView(scene, extent)                    core, no Vulkan
frameSet = FrameUniforms for the view, in the arena
meshes.prepare(view, frame, cmd)                     copies recorded before the graph, one barrier
graph: forward -> hdr -> tonemap -> backbuffer       (windowed: the swapchain image is a colour attachment)
       forward -> hdr -> tonemap -> display -> readback -> imported HostRead buffer   (headless)
compile; execute
submit2: wait imageAvailable at firstUseStage(backbuffer); signal renderFinished[image]; fence
present                                              suboptimal or out of date: recreate
headless: wait the fence, copy the readback buffer into lastFrame()
```

`vkDeviceWaitIdle` appears only in swapchain recreation and teardown. The fence is reset right before
submitting, so an early return never leaves it unsignalled. Render-finished semaphores are per swapchain
image (a frame slot can come round again before the presentation engine has consumed its wait) and only ever
grow in number, so none is destroyed while a present may still use it. Swapchains are recreated from the
previous one.

## 5. Pipelines, shaders and descriptors

- `phox::PipelineDesc` (`include/phox/pipeline/PipelineDesc.hpp`): shaders by name, vertex layout, topology,
  raster, depth and blend state, attachment formats, push constant ranges and the set layouts used. Equal
  descriptions (ignoring the name) hash equal.
- `vk::ShaderLibrary` loads `<name>.spv` from `PHOX_SHADER_DIR` once.
- `vk::PipelineCache` creates dynamic-rendering pipelines on first request and caches pipeline layouts. It owns
  the fixed set layouts: `Frame` (the per-frame uniform buffer) and `Texture` (a combined image sampler with an
  immutable linear clamp sampler, so a descriptor write needs only a view). The renderer builds its pipelines
  during initialization, so broken shaders select the software fallback instead of failing the first frame.
- `shaders/common.glsl`, mirrored by `include/phox/vulkan/ShaderInterface.hpp`:
  - set 0 `FrameUniforms` (256 bytes): view, proj, viewProj, camera position, light direction with the ambient
    term in w, light colour, resolution;
  - push constants `{vec4 albedo; mat4 model}` (80 bytes, vertex and fragment). The albedo comes first because
    llvmpipe (Mesa 25.1) reads the colour of constant-output fragment shaders from offset 0 on fully covered
    tiles.
- `vk::DescriptorAllocator` is per frame: pools reset together and grow when one runs out.

The abstract `phox::Device` creates buffers and images but not pipelines, which the plan had it do: creating a
pipeline needs the shader library and set layouts, which belong with the cache.

## 6. Scene, view and passes

- `Mesh`: indexed `Vertex {position, normal, uv}` with bounds, a never-reused id and a version for caching.
  Built-ins: `triangle`, `cube`, `plane`; `loadMesh()` reads model files through assimp (OBJ in this build).
- `Object`: name, `shared_ptr<const Mesh>`, `Material {name, albedo}`, `Transform` (scale, rotate, translate).
- `Scene`: objects, `Camera` (position, orientation, field of view, near and far; `lookAt`), `DirectionalLight`,
  clear colour and `ToneMapping` (exposure, then clamp, Reinhard or ACES).
- `extractView(scene, extent)` computes the matrices from the target's aspect ratio, culls objects whose
  bounds lie outside a frustum plane, and sorts the rest front to back.
- `SampleScenes` gives the app and the tests the same scenes with deterministic `animate(scene, t)`.

Feature passes are free functions in `phox::vk` that add themselves to a frame's graph:

- `addForwardPass(graph, view, frameSet, color, depth, meshes, pipelines)`: clears, then draws each item with
  per-draw push constants and indexed draws, lit two-sided by the directional light.
- `addTonemapPass(graph, hdr, target, mapping, pipelines)`: a full-screen triangle reading the HDR image with
  `texelFetch`; it writes every pixel, so the target's load op is DontCare.
- `addReadbackPass(graph, source, buffer)`: copies an 8-bit image into a buffer.

## 7. Upload and caches

- `UploadArena`: one host-visible, persistently mapped buffer per frame context, bump-allocated and reset at
  frame start. A frame that overflows gets extra blocks, merged into one larger block next time.
- `MeshCache::prepare(view, frame, cmd)` uploads meshes that are new or whose version changed: staging through
  the arena, `vkCmdCopyBuffer` into device-local buffers, then one TRANSFER to VERTEX_INPUT | INDEX_INPUT
  barrier. Replaced buffers go to the frame's deletion queue; meshes not drawn for 120 frames are evicted.
- `vk::Device::makeBuffer` / `makeImage` are the only callers of VMA (memory: DeviceLocal, Upload, Readback).

## 8. Fallback, logging and testing

- The software rasterizer (`phox/SoftwareRasterizer.hpp`) renders a `RenderView` with the GPU's projection,
  depth test, two-sided lighting and tone mapping. The plan expected to drop parity with the GPU; it turned
  out cheap to keep, and the golden test checks it (one unit per channel, apart from a pixel on an edge).
- `phox::log` writes levelled messages to stderr through fmt; `PHOX_LOG` sets the level. Debug mode turns on
  the validation layer with synchronization validation through `VK_EXT_layer_settings`, and counts its errors
  and warnings in `Renderer::info()`. Loader messages are not counted.
- Goldens are rendered on lavapipe at 320x240 and compared with a tolerance of 2 per channel on all but 0.5% of
  pixels; the RTX 3080 matches lavapipe within one unit apart from a pixel on an edge.

## 9. Deviations from the plan

| Plan | As built | Why |
|---|---|---|
| Dynamic rendering and synchronization2 in milestone 3 | In milestone 2, with the graph | The graph milestone's "no framebuffer in the scene pass" needed dynamic rendering; doing it later meant writing a framebuffer cache to delete it |
| Readback through a graph buffer in milestone 5 | In milestone 2 | Buffers became graph resources there; the hand-written host barrier went with it |
| Logging in milestone 6 | In milestone 1 | The frame-loop rewrite touched every log call anyway |
| `compile(Extent2D)` | `RenderGraph(Extent2D)`; `compile()` | `execute()` can compile on demand |
| `beginGraphics` / `endGraphics` | `beginPass` / `endPass` for every pass | Also carries debug labels for non-graphics passes |
| Pool reuse after the frames in flight | Immediate reuse | Safe on one queue (section 3.5) and needs fewer images |
| Time in the frame uniforms | Left out | No shader uses it, and it would make goldens depend on wall time |
| Windowed frame with three passes | Two: forward, then tonemap straight into the swapchain image | The blit pass became unnecessary |
| Install workflow kept | Removed | The package installs neither its shaders nor the volk and VMA headers its Vulkan headers include |

## 10. File layout

```
include/phox/                 CpuImage, Log, Math, Resources, RenderGraph, RenderView, Scene, Camera, SampleScenes,
                              MeshLoader, SoftwareRasterizer, Renderer, Device, Buffer, Image, Window
include/phox/pipeline/        PipelineDesc, Pipeline, Shader
include/phox/vulkan/          Renderer, Device, Buffer, Image, Swapchain, FrameContext, UploadArena,
                              DescriptorAllocator, TransientPool, GraphExecutor, PipelineCache, ShaderLibrary,
                              ShaderInterface, MeshCache, Conversions, VulkanCommon
include/phox/vulkan/passes/   ForwardPass, TonemapPass, ReadbackPass
shaders/                      common.glsl, forward.vert/.frag, fullscreen.vert, tonemap.frag
test/source/                  render_graph_test, render_view_test, scene_test, golden_test
test/golden/                  triangle, triangle_turned, cubes, cubes_aces
```

## 11. Next, and where each thing plugs in

- **Installable package.** Install the compiled shaders with a runtime lookup (an environment override or a
  path relative to the library), and export or bundle volk and VMA.
- **Textures and materials.** A `TextureCache` shaped like `MeshCache` with a one-time transition to Sampled;
  a material set (set 1) with a uniform buffer and texture indices; then bindless textures (set 2).
- **Aliasing.** Transients with disjoint lifetimes share one VMA allocation (`vmaCreateAliasingImage`). Compile
  already knows the lifetimes; the executor adds aliasing barriers.
- **Async compute.** A per-pass queue in `PassBuilder`; compile emits semaphores and ownership transfers.
- **Pass reordering.** A topological sort over versions that shortens lifetimes. Versioned handles make this
  internal.
- **Shader reflection.** SPIRV-Reflect fills set layouts and push constant ranges from the SPIR-V.
- **Debug overlay.** An ImGui pass that writes the backbuffer after tonemap.
- **sRGB output.** The swapchain is UNORM and colours are treated as display values; an sRGB swapchain with
  linear shading is the correct long-term choice and will change every golden.
