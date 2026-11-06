# Phox architecture

Status: implemented, 2026-10-28. This started as a plan written against commit `f746304`; the six milestones
it laid out are now in the history (`8d30082` to `60a92e0`). The document describes the design as built, why
it is shaped this way, and where the implementation departed from the plan. Section 11 lists what comes next;
section 12 describes the Metal backend added on 2026-11-04.

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
- `phox::mtl::` is the Metal backend (Apple platforms), the same shape as `phox::vk::` (section 12).

Two rules keep the design small:

1. **The graph tracks resources whose state changes inside a frame**: transient targets, the swapchain image,
   the readback buffer. Resources that stay in one state once uploaded (mesh buffers, later textures) live in
   caches and get one barrier at upload.
2. **The graph is rebuilt every frame.** It is bookkeeping over cached objects (pipelines, pooled images,
   uploaded meshes), so rebuilding costs little and there is no frame-to-frame graph state to get wrong.

## 2. Decisions

| # | Decision | Choice | Why |
|---|---|---|---|
| 1 | Abstraction boundary | Descriptions and handles are backend-free; command recording is backend-specific, inside pass bodies | A portable command API is a project of its own. The Metal backend (section 12) reimplements only the executor, the caches and the pass bodies |
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
are on. Pass bodies get a `vk::PassContext`: command buffer, `image`, `view`, `mipView` (one mip of a
transient, created on first use and kept with the pooled image), `extent`, `buffer`, the device and the frame
context. On devices with mesh shaders, the shader stages of graphics passes include the task and mesh
stages, so barriers cover what task shaders read and write. A pass may order its own work with barriers of
its own (the depth pyramid does, between mips); the graph sees the pass's usage of the whole resource.

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
visibility.prepare(view, meshes, frame)              occlusion culling: words of meshlet visibility bits per draw
graph: forward -> hdr -> tonemap -> backbuffer       (windowed: the swapchain image is a colour attachment)
       forward -> hdr -> tonemap -> display -> readback -> imported HostRead buffer   (headless)
       with occlusion culling, forward is (clear visibility ->) forward early -> depth pyramid -> forward late
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

- `phox::PipelineDesc` (`include/phox/pipeline/PipelineDesc.hpp`): shaders by name (a vertex shader, or a mesh
  shader with an optional task shader), vertex layout, topology, raster, depth and blend state, attachment
  formats, push constant ranges and the set layouts used. Equal descriptions (ignoring the name) hash equal.
- `vk::ShaderLibrary` loads `<name>.spv` from `PHOX_SHADER_DIR` once.
- `vk::PipelineCache` creates dynamic-rendering pipelines, and compute pipelines (a description with only
  `computeShader`), on first request and caches pipeline layouts. It owns the fixed set layouts: `Frame` (the
  per-frame uniform buffer and the frame's material table), `Texture` (a combined image sampler with an
  immutable linear clamp sampler, so a descriptor write needs only a view), `MeshletVisibility` and
  `DepthPyramid` (task stage; only on devices with mesh shaders), `DepthPyramidBuild` (compute) and
  `Bindless` (the texture table, 6.3). The renderer builds its pipelines during initialization, so broken
  shaders select the software fallback instead of failing the first frame.
- `shaders/common.glsl`, mirrored by `include/phox/ShaderInterface.hpp` (shared with the Metal backend) and, for
  the meshlet path, `include/phox/vulkan/ShaderInterface.hpp`:
  - set 0, binding 0 `FrameUniforms` (352 bytes): view, proj, viewProj, camera position, light direction with
    the ambient term in w, light colour, resolution, and the six world-space frustum planes for meshlet
    culling. Visible to the task and mesh stages too when the device has them;
  - set 0, binding 1: the frame's `MaterialData` array (80 bytes each, std430), one entry per drawn item;
  - set 1 (`SetLayout::Bindless`): the texture table (6.3);
  - push constants `DrawPushConstants {mat4 model; uint material}` (80 bytes, vertex stage);
  - on the meshlet path, `MeshletDrawPushConstants` (128 bytes, task and mesh): the model matrix, the camera in
    object space, the device addresses of the mesh's vertices and of its meshlets (followed by their data),
    the meshlet count, the culling flags, the draw's first word of visibility bits (6.2), the material
    index and the cluster LOD threshold (6.4). Code defining `PHOX_MESHLET_DRAW` gets this block, `PHOX_VERTEX_DRAW` the other. The vertex and
    mesh shaders pass the material index on as a flat varying, so `forward.frag` has no push constants and
    serves both paths.
- `vk::DescriptorAllocator` is per frame: pools reset together and grow when one runs out.

The abstract `phox::Device` creates buffers and images but not pipelines, which the plan had it do: creating a
pipeline needs the shader library and set layouts, which belong with the cache.

## 6. Scene, view and passes

- `Mesh`: indexed `Vertex {position, normal, uv, tangent}` (48 bytes) with bounds, a never-reused id and a
  version for caching. Zero tangents are generated from the texture coordinates (`generateTangents`).
  Built-ins: `triangle`, `cube`, `plane`, `torus`, `sphere`; `loadMesh()` reads OBJ through assimp, and
  `loadGltf()` whole glTF scenes (6.3).
- `Object`: name, `shared_ptr<const Mesh>`, `Material` (6.3), `Transform` (scale, rotate, translate).
  Materials are double-sided by default; single-sided ones have their back faces culled. Each
  object also has a never-reused `id()` that renderers key per-object state on (meshlet visibility); copies
  get a new one, moves keep it.
- `Scene`: objects, named by `ObjectId {index, generation}` handles that go stale on removal (a slot table over
  a dense object array, so view extraction iterates contiguously; names are only for `find` and debugging),
  `Camera` (position, orientation, field of view, near and far; `lookAt`), `DirectionalLight`,
  clear colour and `ToneMapping` (exposure, then clamp, Reinhard or ACES).
- `extractView(scene, extent)` computes the matrices and frustum planes from the target's aspect ratio, culls
  objects whose bounds lie outside a frustum plane, and sorts the rest: opaque and alpha-masked items front
  to back (`draws`), blended ones back to front (`transparent`). A draw culls back faces when its material
  is single-sided and its transform does not mirror the mesh (`DrawItem::cullBackFaces`).
- `SampleScenes` gives the app and the tests the same scenes with deterministic `animate(scene, t)`.

Feature passes are free functions in `phox::vk` that add themselves to a frame's graph:

- `addForwardPass(graph, view, sets, color, depth, meshes, pipelines, meshletSettings, occlusion)`: clears,
  then draws each item with per-draw push constants, shaded by its material (6.3) and lit two-sided by the
  directional light: indexed draws, or one `vkCmdDrawMeshTasksEXT` per item when the mesh cache holds
  meshlets (6.1). Two pipelines per path, culling none or back faces, share a layout, so the descriptor sets
  are bound once. With `occlusion` it adds the passes of 6.2 instead of one. Blended items follow in
  "forward blend": alpha blending, depth tested read-only, no occlusion culling.
- `addDepthPyramidPass(graph, depth, pipelines)`: builds the depth pyramid of 6.2 and returns it.
- `addTonemapPass(graph, hdr, target, mapping, pipelines)`: a full-screen triangle reading the HDR image with
  `texelFetch`, then applying the sRGB transfer function unless the target's format does; it writes every
  pixel, so the target's load op is DontCare.
- `addReadbackPass(graph, source, buffer)`: copies an 8-bit image into a buffer.

### 6.1 Mesh shading

On devices with `VK_EXT_mesh_shader` (task and mesh shaders) the forward pass draws meshlets; elsewhere, or
with `RendererConfig::meshShaders` off, it uses the vertex pipeline. Both paths render the same images.

- **Meshlets** (`phox/Meshlets.hpp`, core): `optimizeIndexOrder` reorders a mesh's indices for the vertex
  cache with meshoptimizer; `buildMeshlets` splits them into meshlets of at most 64 vertices and 124
  triangles (meshoptimizer, cone weight 0.25), orders each for locality and computes its bounding sphere and
  normal cone. A `Meshlet` is laid out as the shaders read it (112 bytes, with its LOD fields, 6.4).
  `MeshletMesh::data` holds each
  meshlet's vertex indices followed by its triangles, packed one per `uint` as three 8-bit local indices, so
  no 8-bit storage feature is needed.
- **Buffers**: the meshlet path reads the vertex buffer and the meshlet buffer (meshlets, then their data)
  through buffer device addresses (Vulkan 1.3 guarantees `bufferDeviceAddress`) in push constants: no
  descriptor writes per draw.
- **`forward.task`**: 32 meshlets per workgroup, one per invocation. A meshlet is dropped if its bounding
  sphere (moved into world space, radius scaled by the transform's largest axis) is outside a frustum plane,
  or, for back-face-culled draws, if its normal cone faces away from the camera. The cone test runs in object
  space against the camera transformed by the inverse model matrix: facing is affine-invariant for
  non-mirroring transforms, so the test is exact under non-uniform scale. The survivors are compacted in
  meshlet order through shared memory, not atomics, so the draw order, and with it the image, is the same on
  every run.
- **`forward.mesh`**: one workgroup of 64 invocations per surviving meshlet; each transforms at most one
  vertex, with the same expressions as `forward.vert`, and writes at most two triangles.
- The device enables `maintenance4` as well (also guaranteed by 1.3): SPIR-V built for Vulkan 1.3, which the
  task and mesh shaders need, declares workgroup sizes with `LocalSizeId`.

Measured on an RTX 3080 (`PhoxSample --scene field --size 1920x1080 --frames 200`: 234 of 576 tori survive
object culling, 15.3 million triangles), forward pass GPU time: vertex shaders 1.09 to 1.15 ms, mesh shaders
without meshlet culling 1.07 ms, with it 0.86 ms. Mesh shading on its own is about even with the vertex
pipeline; the gain is the culling. The three images are identical. Back-facing culling depends on how
finely a mesh is tessellated: on the benchmark's 65k-triangle torus, about 42% of meshlets face away from a
typical viewpoint, against 14% for a 4k-triangle one. Occlusion culling (6.2) brings the forward work down
to 0.64 ms.

### 6.2 Occlusion culling

With mesh shaders, meshlet culling and a D32 depth buffer (`RendererConfig::occlusionCulling`, on by
default), meshlets hidden behind others are culled too, in two phases:

1. **forward early** draws the meshlets that were visible at the end of the previous frame, with frustum
   and cone culling. From one frame to the next that is nearly the whole visible set.
2. **depth pyramid** (compute) builds a hierarchical depth buffer from its depth: mip 0 is the largest
   power-of-two size not above the target's, each texel the farthest depth of every depth pixel it overlaps
   (up to 3 x 3); each further mip keeps the farthest of 2 x 2, down to 1x1. Plain fetches, no min/max
   sampler (optional for depth formats). One pass, with barriers of its own between mips.
3. **forward late** tests every meshlet, frustum and cone first, then occlusion: the bounding sphere's
   screen rectangle (`phox::projectSphere`, exact for a symmetric perspective, mirrored in the shader) is
   looked up at the mip where it spans at most one texel, so it touches at most 2 x 2; the meshlet is
   hidden if the sphere's nearest depth is beyond the farthest of those. It records which meshlets are
   visible and draws those the early pass did not.

This is exact, not approximate: a meshlet with a visible pixel is nearer there than whatever the early pass
drew, so it passes the late test against a pyramid that holds the farthest of those depths. Spheres reaching
the near plane are kept. The goldens are rendered with it, and must match the other paths.

- **Visibility bits** (`vk::MeshletVisibility`): one bit per meshlet, in whole 32-bit words per draw, so each
  task shader workgroup reads and writes its own word without atomics. Draws are keyed by `Object::id()`,
  which is stable across frames (draw items are re-sorted every frame). Ranges come from a bump allocator;
  when it is full, the store starts over with every bit cleared (a "clear visibility" transfer pass),
  growing to twice what the frame needs. The bits are a hint, so starting over only costs one frame of
  culling. The buffer is imported into each frame's graph (StorageWrite to StorageWrite; Undefined when
  cleared), read by the early pass and written by the late one.
- **Task shaders**: `forward_task.glsl` built three ways (`forward.task`, `forward_early.task`,
  `forward_late.task`), so each pipeline declares only the sets it uses: after set 1 (the texture table,
  6.3) the late one adds set 2 (visibility) and set 3 (the pyramid with a nearest sampler), the early one set 2.
- **Push constants**: to make room for the draw's visibility offset in 128 bytes, a mesh's meshlets and their
  data share one buffer, and the meshlets' offsets count from its start.

On the RTX 3080 (`field`, 1080p, 200 frames of one pose): forward early 0.56 ms, depth pyramid 0.05 ms,
forward late 0.03 ms, 0.64 ms in all, against 0.87 ms with frustum and cone culling only and 1.12 to 1.17 ms
with vertex shaders. With the scene animating (`--animate`) the late pass draws more of the frame's newly
visible meshlets and takes 0.04 ms; the total stays at 0.65 ms. The four images are identical.

### 6.3 Materials and textures

Phox shades in linear light with glTF 2.0's metallic-roughness model.

- `Material` holds the glTF factors (base colour, metallic, roughness, emissive, normal scale, occlusion
  strength), the alpha mode (`Opaque`, `Mask` with a cutoff, `Blend`), `doubleSided`, and up to five
  `shared_ptr<const Texture>`s: base colour (sRGB), metallic-roughness (g, b), normal, occlusion (r) and
  emissive (sRGB). Colours are linear; `phox/Color.hpp` converts sRGB-picked ones (`srgb(r, g, b)`).
- `Texture` is immutable RGBA8 with a full mip chain built on the CPU when it is made (a box filter in
  linear light for sRGB, renormalised for normal maps), its `TextureEncoding`, its `SamplerDesc` (filters
  and wrap modes) and a never-reused id. Building mips on the CPU makes every backend and the software
  rasterizer sample the same texels, which keeps goldens comparable.
- The BRDF is in `shaders/pbr.glsl`, mirrored by `phox::shadePbr` (`core/Shading.cpp`) and
  `shaders/metal/pbr.h`: GGX distribution, height-correlated Smith visibility, Schlick Fresnel, Lambert
  diffuse. The light colour is irradiance / pi, so a white Lambertian surface facing a white light reflects
  1, as the old flat shading did. The ambient term lights `(diffuse colour + F0) * occlusion` evenly, a
  stand-in until image-based lighting.
- **Output encoding.** The tonemap pass applies the sRGB transfer function (`TonemapPushConstants::encodeSrgb`)
  unless the target's format is sRGB, and `applyToneMapping` does the same on the CPU. Before PBR, colours
  were written as display values; the sample scenes now give theirs through `srgb()` and look as before.

On Vulkan:

- **Texture table.** One descriptor set (`SetLayout::Bindless`, set 1) holds an array of combined image
  samplers, `min(4096, device limits)` long, partially bound and update-after-bind. `vk::TextureCache`
  (shaped like `MeshCache`) uploads each texture once on first use: staging through the arena, copies of
  every mip, a barrier to SHADER_READ_ONLY, then a write of its slot with a sampler made from its
  `SamplerDesc`. Slots of textures unused for 120 frames are freed; no frame in flight can still read them.
  This needs descriptor indexing (runtime arrays, partial binding, update-after-bind and non-uniform
  indexing of sampled images), which nearly every Vulkan 1.2 device, lavapipe and MoltenVK have; device
  selection requires it.
- **Material table.** Each frame uploads one `MaterialData` per drawn item (`draws` then `transparent`) to
  the arena, with texture fields holding table slots or `kNoTexture`, and binds it at set 0, binding 1.
  Draws push their item's index.

Elsewhere:

- **glTF.** `loadGltf(path, scene)` (cgltf, stb_image) adds an object per mesh primitive per node, placed by
  the node's world transform, with its material and textures; primitives and textures used several times
  are shared. It handles `.gltf` and `.glb`, data URIs, triangle strips and fans, missing normals (flat, as
  the spec says) and tangents (generated), sampler modes and `KHR_materials_emissive_strength`. It ignores
  skins, morph targets, animations, cameras, lights, `KHR_texture_transform` and other texture coordinate
  sets, and lists what it ignored in `GltfImport::warnings`; files that require an unsupported extension
  (Draco, meshopt, KTX2) are refused. `makeGltfViewerScene` puts a file on a turntable (`PhoxSample --mesh`).
- **Software rasterizer.** Renders into a linear HDR buffer and tone maps at the end, like the GPU, so blended
  items composite in the same space. It shades with `shadeMaterial`, the C++ twin of `forward.frag`, but
  samples only the first mip; the golden cases with textures allow more differing pixels for that, and for
  GPUs choosing mips differently.

### 6.4 Cluster LOD

With mesh shaders, each mesh carries a cluster LOD hierarchy, the scheme of Unreal's Nanite: for any view,
the task shader picks, for each part of the mesh, the coarsest meshlets whose geometric error stays under
`RendererConfig::lodErrorPixels` (1 by default) on screen. Different parts of one mesh are drawn at
different levels, without cracks between them. `meshLod` turns it off (`PhoxSample --lod off`).

**Building** (`buildMeshletLod`, core; on upload in `vk::MeshCache`, with meshoptimizer 1.3):

1. Level 0 is `buildMeshlets` on the mesh.
2. While more than one meshlet is left: partition the current meshlets into groups of about 16 that share
   vertices (`meshopt_partitionClusters`, by position so seams do not separate neighbours); merge each
   group's triangles and simplify them to about half (`meshopt_simplifyWithAttributes`, positions only, so
   the error is geometric), then split the result into meshlets again. A group that keeps more than 85% of
   its triangles is stuck and stays as it is.
3. While simplifying, every vertex position used by more than one group is locked, and so is each group's
   open border (`meshopt_SimplifyLockBorder`), which also covers the border with groups that got stuck at an
   earlier level. Group borders therefore never move, and the next level's groups, partitioned anew, are
   different, so last level's borders get simplified then.

Every level indexes the mesh's own vertices; only meshlet data is added, about as much again as level 0
(the 65k-triangle torus: 11 levels, 1473 meshlets, 131k triangles in all, built in about 100 ms).

**Errors and bounds.** A group's *simplified bounds* are a sphere enclosing its meshlets' own LOD spheres and
an error of at least the largest of theirs plus what the simplification moved (absolute, object space).
Each meshlet stores two of these (`Meshlet::lodSphere`/`lodError` and `parentSphere`/`parentError`): the
simplified bounds of the group that made it (zero error at level 0), and those of the group it is part of
(`kLodNoParent` if that group got stuck). The bounds enclose rather than fit the geometry, so spheres and
errors never shrink towards coarser levels.

**Selection** (`lodSelected`, mirrored in `forward_task.glsl`): a meshlet is drawn if its own error projects
to at most the threshold and its parent's to more. The projection is `error / (distance to the sphere) *
proj[1][1] * height / 2`: the error in pixels at the nearest point of the sphere, rotation invariant, in
object space (exact under uniform scale). Because of the containment above it never grows from parent to
child, so along any chain of refinements exactly one meshlet passes; and every meshlet one group made
shares that group's bounds bit for bit, so they all decide alike (the shader computes it `precise`, so
the compiler cannot evaluate the shared bounds two ways). The drawn meshlets cover the surface exactly once.
The selection runs before culling in the task shader, over every level's meshlets; occlusion culling (6.2)
tracks visibility per meshlet as before, so a part that changes level is treated as newly visible.

**Testing.** `render_view` checks the hierarchy (levels, halving, monotonic errors and nested spheres) and
that for a closed, seamed torus the selected triangles form a closed surface (every edge, by position, used
once in each direction) from a range of distances. `golden` runs the mesh paths without LOD against the full-
detail goldens (culling must not change the picture), then once with LOD against `<case>.lod.ppm`, made on
lavapipe; the RTX 3080 matches those within one level per channel.

Measured on the RTX 3080 (`PhoxSample --scene field --size 1920x1080 --frames 40 --stats`): of the 15.3
million triangles in the 234 tori that survive object culling, LOD at 1 pixel draws 152 thousand (1%);
forward early goes from 0.66 ms to 0.08 ms, and early, pyramid and late together from 0.75 ms to 0.20 ms.
Against full detail, about 6% of pixels differ by more than 2 levels: silhouettes move by up to a pixel,
and shading follows the coarser triangles. The DamagedHelmet sample (15k triangles at 800 x 600) keeps 90%
at 1 pixel and 51% at 8.

What Nanite does beyond this, and where it would go here:

- **Hierarchical culling.** The task shader tests every meshlet of every level, about twice level 0's
  count. A bounding volume hierarchy over groups, traversed in a persistent compute pass, would skip whole
  subtrees that are too fine or too coarse (meshoptimizer's clusterlod example builds one).
- **GPU-driven instances.** Objects are still culled and drawn one by one from the CPU (see 11).
- **Visibility buffer and software rasterization.** Small triangles cost the hardware rasterizer whole 2 x 2
  quads; Nanite rasterizes them in compute with 64-bit atomics into a visibility buffer and shades each pixel
  once afterwards.
- **Streaming.** Every level stays resident; pages of groups could be loaded on demand instead.

## 7. Upload and caches

- `UploadArena`: one host-visible, persistently mapped buffer per frame context, bump-allocated and reset at
  frame start. A frame that overflows gets extra blocks, merged into one larger block next time.
- `MeshCache::prepare(view, frame, cmd)` uploads meshes that are new or whose version changed: staging through
  the arena, `vkCmdCopyBuffer` into device-local buffers, then one barrier from TRANSFER to VERTEX_INPUT |
  INDEX_INPUT, or to the task and mesh shaders' storage reads for meshlets. The vertex path uploads indices in
  vertex cache order; the meshlet path uploads meshlets instead (6.1). Replaced buffers go to the frame's
  deletion queue; meshes not drawn for 120 frames are evicted.
- `TextureCache::prepare(view, frame, cmd)` does the same for textures (6.3), which never change.
- `vk::Device::makeBuffer` / `makeImage` are the only callers of VMA (memory: DeviceLocal, Upload, Readback).

## 8. Fallback, logging and testing

- The software rasterizer (`phox/SoftwareRasterizer.hpp`) renders a `RenderView` with the GPU's projection,
  depth test, back-face culling, two-sided lighting with perspective-correct normal interpolation, and tone
  mapping. The plan expected to drop parity with the GPU; it turned
  out cheap to keep, and the golden test checks it (one unit per channel, apart from a pixel on an edge).
- `phox::log` writes levelled messages to stderr through fmt; `PHOX_LOG` sets the level. Debug mode turns on
  the validation layer with synchronization validation through `VK_EXT_layer_settings`, and counts its errors
  and warnings in `Renderer::info()`. Loader messages are not counted.
- Goldens are rendered on lavapipe at 320x240 and compared with a tolerance of 2 per channel on all but 0.5% of
  pixels; the RTX 3080 matches lavapipe within one unit apart from a pixel on an edge. The golden test renders
  every case with vertex shaders, with mesh shaders and occlusion culling, and with mesh shaders and frustum
  and cone culling only (the mesh runs are skipped on devices without them unless
  `PHOX_REQUIRE_MESH_SHADERS=1`); all must match the same images. Each case renders three frames, so the
  occlusion run uses both empty and inherited visibility bits.
- `vk::PassTimer` (one per frame context) writes a timestamp pair around each graph pass; the times are read
  once the frame's fence has signalled and reported by `Renderer::gpuTimings()`.

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
include/phox/                 CpuImage, Log, Math, Color, Resources, RenderGraph, RenderView, Scene, Camera, Texture,
                              Shading, ShaderInterface, SampleScenes, MeshLoader, GltfLoader, Meshlets,
                              SoftwareRasterizer, Renderer, Device, Buffer, Image, Window
include/phox/pipeline/        PipelineDesc, Pipeline, Shader
include/phox/vulkan/          Renderer, Device, Buffer, Image, Swapchain, FrameContext, UploadArena,
                              DescriptorAllocator, TransientPool, GraphExecutor, PipelineCache, ShaderLibrary,
                              ShaderInterface, MeshCache, TextureCache, MeshletVisibility, PassTimer, Conversions,
                              VulkanCommon
include/phox/vulkan/passes/   ForwardPass, DepthPyramidPass, TonemapPass, ReadbackPass
include/phox/metal/           Renderer, Device, Buffer, Image, TransientPool, GraphExecutor, PipelineCache,
                              ShaderLibrary, ShaderInterface, MeshCache, TextureCache, Conversions, MetalCommon
include/phox/metal/passes/    ForwardPass, TonemapPass, ReadbackPass
shaders/                      common.glsl, pbr.glsl, forward.vert/.frag/.mesh, forward_task.glsl with forward.task,
                              forward_early.task and forward_late.task, depthpyramid.comp, fullscreen.vert,
                              tonemap.frag
shaders/metal/                common.h, pbr.h, forward.metal, fullscreen.metal, tonemap.metal
test/source/                  render_graph_test, render_view_test, scene_test, golden_test
test/golden/                  triangle, triangle_turned, cubes, cubes_aces, tori, materials; each also .lod.ppm
```

## 11. Next, and where each thing plugs in

- **Object-level occlusion culling on the GPU.** Objects are culled against the frustum on the CPU, and every
  meshlet of a surviving object is tested on the GPU. A compute pass over objects, writing indirect mesh task
  draws, would cull whole hidden objects against the pyramid first.
- **Image-based lighting.** An HDR environment map, prefiltered for specular and convolved for diffuse, with
  a BRDF lookup table, replaces the ambient term in `shadePbr`; smooth metals currently reflect nothing.
- **Shadows.** Cascaded shadow maps for the directional light: a depth pass per cascade before "forward",
  sampled with PCF in `forward.frag`.
- **Instancing.** A draw per object pushes 128 bytes; many instances of one mesh would rather read transforms
  from a buffer. With the remaining Nanite pieces (6.4): hierarchical cluster culling, a visibility buffer
  with software rasterization of small triangles, and streaming.

- **Installable package.** Install the compiled shaders with a runtime lookup (an environment override or a
  path relative to the library), and export or bundle volk and VMA.
- **Aliasing.** Transients with disjoint lifetimes share one VMA allocation (`vmaCreateAliasingImage`). Compile
  already knows the lifetimes; the executor adds aliasing barriers.
- **Async compute.** A per-pass queue in `PassBuilder`; compile emits semaphores and ownership transfers.
- **Pass reordering.** A topological sort over versions that shortens lifetimes. Versioned handles make this
  internal.
- **Shader reflection.** SPIRV-Reflect fills set layouts and push constant ranges from the SPIR-V.
- **Debug overlay.** An ImGui pass that writes the backbuffer after tonemap.

## 12. Metal backend

`include/phox/metal/`, `source/phox/metal/*.mm` (Objective-C++ with ARC), `shaders/metal/`. Built when
`PHOX_ENABLE_METAL` is on, which is the default on Apple platforms. It is selected with `RendererType::Metal`
(`--backend metal`). Vulkan keeps working there through MoltenVK.

The backend reuses everything above the executor unchanged: `Scene`, `extractView`, `RenderGraph` and its
compile, `PipelineDesc` and the three feature passes' graph declarations. Only the pass bodies and the
objects under them are Metal-specific. Its pieces map one to one onto the Vulkan ones:

| Vulkan | Metal | Difference |
|---|---|---|
| `vk::GraphExecutor` | `mtl::GraphExecutor` | One encoder per pass, chosen by pass type (render, compute, blit); a render pass descriptor is built from the declared attachments. `barriers()` records nothing (below) |
| synchronization2 barriers, layouts | none | Every resource uses Metal's automatic hazard tracking, which orders encoders that touch the same resource. Metal has no layouts |
| `TransientPool` | `mtl::TransientPool` | Same policy, and immediate reuse is safe for the same reason: one queue, with ordering provided by hazard tracking instead of a barrier |
| `FrameContext` ring, fences | a dispatch semaphore of 2 | A frame's completion handler gives its slot back. Command buffers keep the resources they reference alive, so there is no deletion queue |
| `UploadArena`, descriptor sets | `setVertexBytes` / `setFragmentBytes` | `FrameUniforms`, per-draw constants and each draw's `MaterialData` go inline in the command buffer; the HDR texture is bound directly |
| `TextureCache`, texture table | `mtl::TextureCache` | No table: each draw binds its material's five textures and samplers (texture and sampler 0 to 4), a 1 x 1 white placeholder where it has none, because Metal's API validation wants every declared texture bound. Shared textures on Apple silicon, managed ones elsewhere, filled with `replaceRegion` |
| `PipelineCache`, set layouts, push constants | `mtl::PipelineCache` | Same descriptions. `SetLayout::Frame` is buffer 0, per-draw constants buffer 1, the draw's material buffer 2, vertices buffer 30, `SetLayout::Texture` texture 0 (`metal/ShaderInterface.hpp`) |
| `ShaderLibrary` (SPIR-V) | `mtl::ShaderLibrary` (MSL) | `"forward.vert"` is function `forward_vert` in `forward.metal`, compiled from source at startup. `#include "common.h"` is inlined by the loader |
| `MeshCache` (device-local, copy and barrier) | `mtl::MeshCache` | Shared buffers written directly: unified memory on Apple silicon |
| `Swapchain` | `CAMetalLayer` | The window owns the layer (`Window::metalLayer()`, `WindowConfig::api = GraphicsApi::Metal`); the drawable's texture is imported like the swapchain image |

Decisions specific to this backend:

- **Clip space.** `phox::perspective` flips y for Vulkan's y-down clip space. The MSL vertex shaders flip it
  back (`toMetalClip`), so every vertex lands on the same pixel as in Vulkan and the window-space winding is
  the same: pipelines keep `FrontFace::CounterClockwise`. Depth is [0, 1] in both.
- **Shaders compiled at runtime**, with fast math off so results track the goldens. Precompiling to a
  `.metallib` needs Xcode's Metal Toolchain, which a build cannot assume. MSL has no `inverse()`, so the
  forward shader builds the normal matrix from cofactors. The loader inlines `#include "…"` lines (common.h,
  pbr.h).
- **Validation.** `RendererConfig::debug` sets `MTL_DEBUG_LAYER` before the device is created and reports
  whether it took effect (Metal reads it once per process; a Metal window creates a device first, so
  PhoxSample sets it before creating the window). Metal's layer stops the process on an error instead of
  reporting it, so `validationErrors` counts command buffers that failed.
- **Verification.** `golden_metal` holds the backend to the lavapipe goldens. On an M2 Max its images are
  identical to the Vulkan backend's (MoltenVK) on the same GPU, and the compiled frame graphs are the same.

Next for this backend: memoryless depth (`MTLStorageModeMemoryless`) for attachments that are cleared and
never stored, which the graph already knows; `MTLFence`s and untracked heaps when transients alias; private
mesh buffers through a blit on GPUs without unified memory (Intel Macs); precompiled `.metallib`s when the
toolchain is present.
