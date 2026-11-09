#include "phox/vulkan/GpuScene.hpp"

#include "phox/Log.hpp"
#include "phox/RenderView.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/MeshCache.hpp"
#include "phox/vulkan/ShaderInterface.hpp"
#include "phox/vulkan/TextureCache.hpp"

#include <algorithm>
#include <memory>
#include <string>

namespace phox::vk {

namespace {

std::uint32_t meshletGroups(std::uint32_t meshletCount) {
    return (meshletCount + kMeshletTaskGroupSize - 1) / kMeshletTaskGroupSize;
}

std::uint32_t roundUpPow2(std::uint32_t value) {
    std::uint32_t result = 1;
    while (result < value) result <<= 1;
    return result;
}

// Destroys @p buffer once the frame that may still use it has finished.
void retire(std::unique_ptr<Buffer> &buffer, FrameContext &frame) {
    if (buffer) {
        std::shared_ptr<Buffer> old(std::move(buffer));
        frame.deferred().push([old]() mutable { old.reset(); });
    }
}

} // namespace

GpuScene::GpuScene(Device &device, bool meshlets, bool occlusion)
    : m_device(device), m_meshlets(meshlets), m_occlusion(occlusion && meshlets) {}

GpuScene::~GpuScene() = default;

GpuScene::Frame GpuScene::update(const RenderView &view, const MeshCache &meshes, const TextureCache &textures,
                                 FrameContext &frame) {
    Frame result;

    // Slots are scene indices. The arrays cover every slot up to the highest in use; those without an
    // object stay empty (kNoMesh). They are written straight into the frame's upload arena.
    std::uint32_t slotCount = 0;
    for (const auto *items : {&view.draws, &view.transparent}) {
        for (const DrawItem &draw : *items) slotCount = std::max(slotCount, draw.sceneIndex + 1);
    }
    if (m_slots.size() < slotCount) {
        m_slots.resize(slotCount);
    }
    const auto slotFor = [this](const DrawItem &draw) {
        Slot &slot = m_slots[draw.sceneIndex];
        if (slot.object != draw.objectId) {
            slot = Slot{}; // another object: none of the old one's state applies
            slot.object = draw.objectId;
        }
        return draw.sceneIndex;
    };
    result.instanceCount = slotCount;
    UploadArena &arena = frame.arena();
    const std::size_t arrayLength = std::max<std::uint32_t>(slotCount, 1);
    const UploadArena::Allocation instanceAlloc = arena.allocate(arrayLength * sizeof(GpuInstance), 16);
    auto *instances = static_cast<GpuInstance *>(instanceAlloc.data);
    std::uninitialized_fill_n(instances, arrayLength, GpuInstance{});
    // Only the slots of objects are read, so the rest of the material array may hold anything.
    const VkDeviceSize materialBytes = arrayLength * sizeof(MaterialData);
    const UploadArena::Allocation materialAlloc =
        arena.allocate(materialBytes, m_device.properties().limits.minStorageBufferOffsetAlignment);
    auto *materials = static_cast<MaterialData *>(materialAlloc.data);

    std::vector<GpuMesh> gpuMeshes;
    std::unordered_map<std::uint64_t, std::uint32_t> meshIndex;
    const Mesh *lastMesh = nullptr; // objects often come in runs that share a mesh
    std::uint32_t lastIndex = 0;
    const auto meshFor = [&](const Mesh &mesh) {
        if (&mesh == lastMesh) {
            return lastIndex;
        }
        const auto [it, inserted] = meshIndex.try_emplace(mesh.id(), static_cast<std::uint32_t>(gpuMeshes.size()));
        if (inserted) {
            const MeshCache::GpuMesh &cached = meshes.get(mesh);
            GpuMesh entry;
            entry.sphere = vec4(mesh.bounds().center(), 0.5f * glm::length(mesh.bounds().size()));
            entry.vertices = cached.vertices->deviceAddress();
            if (cached.meshlets) {
                entry.meshlets = cached.meshlets->deviceAddress();
                entry.meshletCount = cached.meshletCount;
            } else {
                entry.indexCount = cached.indexCount;
                entry.firstIndex = cached.firstIndex;
            }
            gpuMeshes.push_back(entry);
        }
        lastMesh = &mesh;
        lastIndex = it->second;
        return lastIndex;
    };
    const auto writeMaterial = [&](std::uint32_t slot, const Material &material) {
        const auto textureSlot = [&](const std::shared_ptr<const Texture> &texture) {
            return texture ? textures.slot(*texture) : kNoTexture;
        };
        MaterialData &data = materials[slot];
        data = materialData(material);
        data.baseColorTexture = textureSlot(material.baseColorTexture);
        data.metallicRoughnessTexture = textureSlot(material.metallicRoughnessTexture);
        data.normalTexture = textureSlot(material.normalTexture);
        data.occlusionTexture = textureSlot(material.occlusionTexture);
        data.emissiveTexture = textureSlot(material.emissiveTexture);
    };

    // Objects the GPU culls: an instance each, and capacity for what the cull passes may append.
    bool restartVisibility = false;
    std::uint32_t workCapacity = 0;
    std::vector<std::uint32_t> drawSlots;
    drawSlots.reserve(view.draws.size());
    for (const DrawItem &draw : view.draws) {
        const std::uint32_t slot = slotFor(draw);
        drawSlots.push_back(slot);
        writeMaterial(slot, *draw.material);
        GpuInstance &instance = instances[slot];
        instance.model = draw.model;
        instance.mesh = meshFor(*draw.mesh);
        instance.flags = draw.cullBackFaces ? kInstanceCullBackFaces : 0u;
        const std::uint32_t groups = m_meshlets ? meshletGroups(gpuMeshes[instance.mesh].meshletCount) : 1u;
        workCapacity += groups;
        if (m_occlusion) {
            Slot &state = m_slots[slot];
            if (state.mesh != draw.mesh->id() || state.meshVersion != draw.mesh->version() || state.visibilityWords != groups) {
                state.mesh = draw.mesh->id();
                state.meshVersion = draw.mesh->version();
                state.visibilityWords = groups;
                if (m_visibility && m_visibilityUsed + groups <= m_visibility->size() / sizeof(std::uint32_t)) {
                    state.visibilityOffset = m_visibilityUsed;
                    m_visibilityUsed += groups;
                } else {
                    restartVisibility = true;
                }
            }
            instance.visibilityOffset = state.visibilityOffset;
        }
    }
    result.drawCapacity = std::max<std::uint32_t>(static_cast<std::uint32_t>(view.draws.size()), 1);
    result.workCapacity = std::max(workCapacity, 1u);

    if (m_occlusion) {
        // Restart the visibility bits: every live slot gets fresh words, in a buffer large enough for them.
        if (restartVisibility) {
            std::uint32_t needed = 0;
            for (std::uint32_t slot : drawSlots) needed += m_slots[slot].visibilityWords;
            const std::uint64_t words = m_visibility ? m_visibility->size() / sizeof(std::uint32_t) : 0;
            if (words < needed) {
                retire(m_visibility, frame);
                const std::uint32_t capacity = std::max(kInitialVisibilityWords, roundUpPow2(needed * 2));
                m_visibility = m_device.makeBuffer({capacity * sizeof(std::uint32_t),
                                                    usageBit(BufferUsage::StorageRead) | usageBit(BufferUsage::StorageWrite) |
                                                        usageBit(BufferUsage::TransferDst),
                                                    MemoryLocation::DeviceLocal, "meshlet visibility", true});
                log::debug("gpu scene: meshlet visibility grown to {} words", capacity);
            }
            m_visibilityUsed = 0;
            for (std::uint32_t slot : drawSlots) {
                m_slots[slot].visibilityOffset = m_visibilityUsed;
                m_visibilityUsed += m_slots[slot].visibilityWords;
                instances[slot].visibilityOffset = m_slots[slot].visibilityOffset;
            }
            result.clearState = true;
        }
        if (!m_history || m_history->size() < m_slots.size() * sizeof(std::uint32_t)) {
            retire(m_history, frame);
            const std::uint32_t capacity =
                std::max(kInitialSlots, roundUpPow2(static_cast<std::uint32_t>(m_slots.size()) * 2));
            m_history = m_device.makeBuffer({capacity * sizeof(std::uint32_t),
                                             usageBit(BufferUsage::StorageRead) | usageBit(BufferUsage::StorageWrite) |
                                                 usageBit(BufferUsage::TransferDst),
                                             MemoryLocation::DeviceLocal, "instance visibility", true});
            result.clearState = true;
        }
        if (m_stateUndefined) {
            result.clearState = true;
            m_stateUndefined = false;
        }
    }

    // Blended objects: materials, then GpuDraws and work in back-to-front order, written here.
    std::vector<GpuDraw> blendDraws;
    for (const DrawItem &draw : view.transparent) {
        const std::uint32_t slot = slotFor(draw);
        writeMaterial(slot, *draw.material);
        const GpuMesh &mesh = gpuMeshes[meshFor(*draw.mesh)];
        GpuDraw gpuDraw;
        gpuDraw.model = draw.model;
        gpuDraw.cameraPosition = glm::inverse(draw.model) * vec4(view.cameraPosition, 1.0f);
        gpuDraw.vertices = mesh.vertices;
        gpuDraw.primitives = m_meshlets ? mesh.meshlets : 0;
        gpuDraw.count = m_meshlets ? mesh.meshletCount : mesh.indexCount;
        gpuDraw.material = slot;
        gpuDraw.flags = draw.cullBackFaces ? kDrawConeCulling : 0u;
        blendDraws.push_back(gpuDraw);
    }
    if (!blendDraws.empty()) {
        result.blendDraws = arena.upload(blendDraws.data(), blendDraws.size() * sizeof(GpuDraw), 16).address;
    }
    for (std::size_t first = 0; first < view.transparent.size();) {
        const bool cull = view.transparent[first].cullBackFaces;
        std::size_t end = first;
        while (end < view.transparent.size() && view.transparent[end].cullBackFaces == cull) ++end;
        BlendRun run;
        run.cullBackFaces = cull;
        if (m_meshlets) {
            std::vector<std::uint32_t> work; // (draw, group) pairs
            for (std::size_t d = first; d < end; ++d) {
                for (std::uint32_t g = 0; g < meshletGroups(blendDraws[d].count); ++g) {
                    work.insert(work.end(), {static_cast<std::uint32_t>(d), g});
                }
            }
            DrawCounters counters;
            counters.drawCount = static_cast<std::uint32_t>(end - first);
            counters.workCount[0] = static_cast<std::uint32_t>(work.size() / 2);
            const std::uint32_t maxX = m_device.meshShaderProperties().maxTaskWorkGroupCount[0];
            const std::uint32_t x = std::min(counters.workCount[0], maxX);
            counters.args[0][0] = x;
            counters.args[0][1] = x == 0 ? 1 : (counters.workCount[0] + x - 1) / x;
            counters.args[0][2] = 1;
            const UploadArena::Allocation countersAlloc = arena.upload(&counters, sizeof(counters), 16);
            run.buffer = countersAlloc.buffer;
            run.argsOffset = countersAlloc.offset + kDrawCountersArgsOffset;
            run.counters = countersAlloc.address;
            run.work = arena.upload(work.data(), std::max<std::size_t>(work.size(), 2) * sizeof(std::uint32_t), 8).address;
        } else {
            std::vector<std::uint32_t> commands; // VkDrawIndexedIndirectCommand per draw
            for (std::size_t d = first; d < end; ++d) {
                const GpuMesh &mesh = gpuMeshes[meshFor(*view.transparent[d].mesh)];
                commands.insert(commands.end(), {mesh.indexCount, 1u, mesh.firstIndex, 0u, static_cast<std::uint32_t>(d)});
            }
            const UploadArena::Allocation commandAlloc =
                arena.upload(commands.data(), commands.size() * sizeof(std::uint32_t), 4);
            run.buffer = commandAlloc.buffer;
            run.argsOffset = commandAlloc.offset;
            run.drawCount = static_cast<std::uint32_t>(end - first);
        }
        result.blendRuns.push_back(run);
        first = end;
    }

    if (gpuMeshes.empty()) {
        gpuMeshes.emplace_back();
    }
    result.indexPool = meshes.indexPool();
    result.drawCount = m_device.drawIndirectCount();
    result.instances = instanceAlloc.address;
    result.meshes = arena.upload(gpuMeshes.data(), gpuMeshes.size() * sizeof(GpuMesh), 16).address;
    result.materials = {materialAlloc.buffer, materialAlloc.offset, materialBytes};
    return result;
}

} // namespace phox::vk
