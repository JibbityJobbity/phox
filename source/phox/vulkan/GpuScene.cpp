#include "phox/vulkan/GpuScene.hpp"

#include "phox/Log.hpp"
#include "phox/RenderView.hpp"
#include "phox/Scene.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/MeshCache.hpp"
#include "phox/vulkan/TextureCache.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
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

// A device-local buffer shaders read and write through its address, and transfers may fill.
std::unique_ptr<Buffer> makeStateBuffer(Device &device, VkDeviceSize bytes, const char *name) {
    return device.makeBuffer({bytes,
                              usageBit(BufferUsage::StorageRead) | usageBit(BufferUsage::StorageWrite) |
                                  usageBit(BufferUsage::TransferDst),
                              MemoryLocation::DeviceLocal, name, true});
}

bool cullsBackFaces(bool doubleSided, const mat4 &model) {
    return !doubleSided && glm::determinant(mat3(model)) > 0.0f;
}

} // namespace

GpuScene::GpuScene(Device &device, bool meshlets, bool occlusion)
    : m_device(device), m_meshlets(meshlets), m_occlusion(occlusion && meshlets) {}

GpuScene::~GpuScene() = default;

std::uint32_t GpuScene::acquireMesh(const std::shared_ptr<const Mesh> &mesh) {
    const auto [it, inserted] = m_meshIndex.try_emplace(mesh->id(), 0u);
    if (inserted) {
        if (!m_freeMeshes.empty()) {
            it->second = m_freeMeshes.back();
            m_freeMeshes.pop_back();
        } else {
            it->second = static_cast<std::uint32_t>(m_meshes.size());
            m_meshes.emplace_back();
        }
        m_meshes[it->second] = MeshEntry{};
        m_meshes[it->second].mesh = mesh;
    }
    ++m_meshes[it->second].users;
    return it->second;
}

void GpuScene::releaseMesh(std::uint32_t index) {
    if (index == kNoMesh) {
        return;
    }
    MeshEntry &entry = m_meshes[index];
    if (--entry.users == 0) {
        // The mesh cache evicts it once no frame has used it for a while.
        m_meshIndex.erase(entry.mesh->id());
        entry = MeshEntry{};
        m_freeMeshes.push_back(index);
    }
}

void GpuScene::releaseTextures(SlotMaterial &material) {
    for (std::uint64_t &id : material.textures) {
        if (id != 0) {
            const auto it = m_textures.find(id);
            if (--it->second.users == 0) {
                m_textures.erase(it);
            }
            id = 0;
        }
    }
}

void GpuScene::release(std::uint32_t index) {
    Slot &slot = m_slots[index];
    releaseMesh(slot.mesh);
    releaseTextures(m_slotMaterials[index]);
    setCounted(slot, false, 0);
    if (slot.blended) {
        m_blendedChanged = true;
    }
    --m_occupied;
    m_tracked[index] = Tracked{};
    const bool instanceDirty = slot.instanceDirty;
    const bool materialDirty = slot.materialDirty;
    slot = Slot{};
    slot.instanceDirty = instanceDirty;
    slot.materialDirty = materialDirty;
    markInstance(index); // empty from now on
}

void GpuScene::markInstance(std::uint32_t index) {
    if (!m_slots[index].instanceDirty) {
        m_slots[index].instanceDirty = true;
        m_instanceDirty.push_back(index);
    }
}

void GpuScene::markMaterial(std::uint32_t index) {
    if (!m_slots[index].materialDirty) {
        m_slots[index].materialDirty = true;
        m_materialDirty.push_back(index);
    }
}

void GpuScene::setCounted(Slot &slot, bool counted, std::uint32_t groups) {
    if (slot.counted) {
        --m_drawCount;
        m_workTotal -= slot.groups;
    }
    slot.counted = counted;
    slot.groups = counted ? groups : 0;
    if (counted) {
        ++m_drawCount;
        m_workTotal += groups;
    }
}

void GpuScene::track(std::uint32_t index, const Object &object, TextureCache &textures, std::uint64_t frameNumber) {
    if (index >= m_slots.size()) {
        m_slots.resize(index + 1);
        m_slotMaterials.resize(index + 1);
        m_tracked.resize(index + 1);
    }
    Tracked &tracked = m_tracked[index];
    tracked.seen = m_updates;
    if (tracked.object == object.id() && tracked.transformRevision == object.transformRevision() &&
        tracked.materialRevision == object.materialRevision()) {
        return; // unchanged
    }
    if (tracked.object != object.id()) {
        if (tracked.object != 0) {
            release(index); // the slot's last object was removed, and this one took its index
        }
        tracked.object = object.id(); // revisions start at 1, so everything below is written
        tracked.seen = m_updates;
        m_slots[index].occupied = true;
        ++m_occupied;
    }
    Slot &slot = m_slots[index];
    if (tracked.transformRevision != object.transformRevision()) {
        tracked.transformRevision = object.transformRevision();
        slot.model = object.transform().matrix();
        const std::shared_ptr<const Mesh> &mesh = object.mesh();
        if (slot.mesh == kNoMesh || m_meshes[slot.mesh].mesh != mesh) {
            releaseMesh(slot.mesh);
            slot.mesh = mesh ? acquireMesh(mesh) : kNoMesh;
            m_blendedChanged = m_blendedChanged || slot.blended;
        }
        markInstance(index);
    }
    if (tracked.materialRevision != object.materialRevision()) {
        tracked.materialRevision = object.materialRevision();
        const Material &material = object.material();
        // Taken before the old ones are let go, so textures both materials use stay resident.
        const std::shared_ptr<const Texture> *used[5] = {&material.baseColorTexture, &material.metallicRoughnessTexture,
                                                         &material.normalTexture, &material.occlusionTexture,
                                                         &material.emissiveTexture};
        std::uint32_t tableSlots[5]{kNoTexture, kNoTexture, kNoTexture, kNoTexture, kNoTexture};
        std::uint64_t textureIds[5]{};
        for (int t = 0; t < 5; ++t) {
            if (*used[t]) {
                const auto [it, inserted] = m_textures.try_emplace((*used[t])->id());
                if (inserted) {
                    it->second.texture = *used[t];
                }
                ++it->second.users;
                textureIds[t] = it->first;
                tableSlots[t] = textures.require(**used[t], frameNumber);
            }
        }
        SlotMaterial &copy = m_slotMaterials[index];
        releaseTextures(copy);
        std::copy(std::begin(textureIds), std::end(textureIds), std::begin(copy.textures));
        copy.data = materialData(material);
        copy.data.baseColorTexture = tableSlots[0];
        copy.data.metallicRoughnessTexture = tableSlots[1];
        copy.data.normalTexture = tableSlots[2];
        copy.data.occlusionTexture = tableSlots[3];
        copy.data.emissiveTexture = tableSlots[4];
        const bool blended = material.alphaMode == AlphaMode::Blend;
        if (blended != slot.blended) {
            slot.blended = blended;
            m_blendedChanged = true;
        }
        slot.doubleSided = material.doubleSided; // decides the instance's pipeline
        markMaterial(index);
        markInstance(index);
    }
}

GpuScene::Frame GpuScene::update(const Scene &scene, const RenderView &view, MeshCache &meshes, TextureCache &textures,
                                 FrameContext &frame, VkCommandBuffer cmd, std::uint64_t frameNumber) {
    Frame result;
    ++m_updates;

    // ---- What changed: objects that arrived, left, moved, or changed mesh or material -----------------
    if (const std::optional<std::span<const std::uint32_t>> changes = scene.changes(m_cursor)) {
        for (std::uint32_t index : *changes) {
            if (const Object *object = scene.objectAt(index)) {
                track(index, *object, textures, frameNumber);
            } else if (index < m_tracked.size() && m_tracked[index].object != 0) {
                release(index);
            }
        }
    } else {
        // The scene cannot say what changed: look at every object.
        const std::span<const Object> objects = scene.objects();
        const std::span<const ObjectId> ids = scene.objectIds();
        for (std::size_t i = 0; i < objects.size(); ++i) {
            track(ids[i].index, objects[i], textures, frameNumber);
        }
        // Each object holds one slot, so more occupied slots than objects means some were removed.
        if (m_occupied > objects.size()) {
            for (std::uint32_t index = 0; index < m_slots.size(); ++index) {
                if (m_tracked[index].object != 0 && m_tracked[index].seen != m_updates) {
                    release(index);
                }
            }
        }
    }

    // ---- Meshes and textures in use stay resident; meshes whose version changed are uploaded again --
    for (const MeshEntry &entry : m_meshes) {
        if (entry.users != 0) {
            meshes.require(*entry.mesh, frameNumber);
        }
    }
    for (const auto &[id, entry] : m_textures) {
        textures.require(*entry.texture, frameNumber);
    }
    meshes.flush(frame, cmd);
    textures.flush(frame, cmd);
    std::vector<std::uint32_t> rebuilt; // meshes whose geometry changed under objects that use them
    for (std::uint32_t index = 0; index < m_meshes.size(); ++index) {
        MeshEntry &entry = m_meshes[index];
        if (entry.users == 0 || entry.version == entry.mesh->version()) {
            continue;
        }
        if (entry.version != 0) {
            rebuilt.push_back(index);
        }
        const Mesh &mesh = *entry.mesh;
        entry.version = mesh.version();
        entry.gpu = GpuMesh{};
        entry.gpu.sphere = vec4(mesh.bounds().center(), 0.5f * glm::length(mesh.bounds().size()));
        if (!mesh.empty()) { // an empty mesh draws nothing: no meshlets, no indices
            const MeshCache::GpuMesh &cached = meshes.get(mesh);
            entry.gpu.vertices = cached.vertices->deviceAddress();
            if (cached.meshlets) {
                entry.gpu.meshlets = cached.meshlets->deviceAddress();
                entry.gpu.meshletCount = cached.meshletCount;
            } else {
                entry.gpu.indexCount = cached.indexCount;
                entry.gpu.firstIndex = cached.firstIndex;
            }
        }
    }
    if (!rebuilt.empty()) {
        // Their work and visibility words change: rewrite the instances that use them.
        for (std::uint32_t index = 0; index < m_slots.size(); ++index) {
            const std::uint32_t mesh = m_slots[index].mesh;
            if (mesh != kNoMesh && std::find(rebuilt.begin(), rebuilt.end(), mesh) != rebuilt.end()) {
                markInstance(index);
            }
        }
    }

    // ---- GPU buffers, grown to cover every slot --------------------------------------------------------
    const auto slotCount = static_cast<std::uint32_t>(m_slots.size());
    if (!m_instances || m_instances->size() < std::max(slotCount, 1u) * sizeof(GpuInstance)) {
        retire(m_instances, frame);
        retire(m_materials, frame);
        const std::uint32_t capacity = std::max(kInitialSlots, roundUpPow2(slotCount * 2));
        m_instances = makeStateBuffer(m_device, capacity * sizeof(GpuInstance), "scene instances");
        m_materials = makeStateBuffer(m_device, capacity * sizeof(MaterialData), "scene materials");
        result.clearInstances = true;
        for (std::uint32_t index = 0; index < slotCount; ++index) {
            if (m_slots[index].occupied) {
                markInstance(index);
                markMaterial(index);
            }
        }
        log::debug("gpu scene: instance and material buffers grown to {} slots", capacity);
    }

    // ---- Instances to rewrite: counted towards the cull passes, given visibility words, and written ----
    UploadArena &arena = frame.arena();
    // Writes the updates of m_instanceDirty for the scene update pass. With @p assign, each slot is first
    // counted and given visibility words; returns false if the words ran out (offsets written are stale).
    const auto writeInstances = [&](bool assign) {
        bool allocated = true;
        const UploadArena::Allocation alloc = arena.allocate(m_instanceDirty.size() * sizeof(InstanceUpdate), 16);
        auto *out = static_cast<std::byte *>(alloc.data);
        for (std::uint32_t index : m_instanceDirty) {
            Slot &slot = m_slots[index];
            slot.instanceDirty = false;
            if (assign) {
                const bool culledOnGpu = slot.occupied && slot.mesh != kNoMesh && !slot.blended;
                const MeshEntry *mesh = slot.mesh != kNoMesh ? &m_meshes[slot.mesh] : nullptr;
                const std::uint32_t groups =
                    !culledOnGpu ? 0u : m_meshlets ? meshletGroups(mesh->gpu.meshletCount) : 1u;
                setCounted(slot, culledOnGpu, groups);
                if (m_occlusion && !culledOnGpu) {
                    slot.visibilityMesh = 0; // words are handed out again when it is counted again
                } else if (m_occlusion &&
                           (slot.visibilityMesh != mesh->mesh->id() || slot.visibilityMeshVersion != mesh->version)) {
                    slot.visibilityMesh = mesh->mesh->id();
                    slot.visibilityMeshVersion = mesh->version;
                    if (m_visibility && m_visibilityUsed + groups <= m_visibility->size() / sizeof(std::uint32_t)) {
                        slot.visibilityOffset = m_visibilityUsed;
                        m_visibilityUsed += groups;
                    } else {
                        allocated = false;
                    }
                }
            }
            InstanceUpdate update;
            update.slot = index;
            if (slot.counted) {
                update.instance.model = slot.model;
                update.instance.mesh = slot.mesh;
                update.instance.flags = cullsBackFaces(slot.doubleSided, slot.model) ? kInstanceCullBackFaces : 0u;
                update.instance.visibilityOffset = m_occlusion ? slot.visibilityOffset : 0u;
            } // otherwise kNoMesh: empty, or drawn by the CPU (blended)
            std::memcpy(out, &update, sizeof(update));
            out += sizeof(update);
        }
        result.instanceUpdates = alloc.address;
        result.instanceUpdateCount = static_cast<std::uint32_t>(m_instanceDirty.size());
        m_instanceDirty.clear();
        return allocated;
    };
    const bool allocated = m_instanceDirty.empty() || writeInstances(true);
    result.drawCapacity = std::max(m_drawCount, 1u);
    result.workCapacity = std::max(m_workTotal, 1u);

    if (m_occlusion) {
        // Restart the visibility bits: every counted slot gets fresh words, in a buffer large enough, and
        // every slot is written again (rare, so simply all of them).
        if (!allocated) {
            const std::uint32_t needed = m_workTotal;
            if (!m_visibility || m_visibility->size() / sizeof(std::uint32_t) < needed) {
                retire(m_visibility, frame);
                const std::uint32_t capacity = std::max(kInitialVisibilityWords, roundUpPow2(needed * 2));
                m_visibility = makeStateBuffer(m_device, capacity * sizeof(std::uint32_t), "meshlet visibility");
                log::debug("gpu scene: meshlet visibility grown to {} words", capacity);
            }
            m_visibilityUsed = 0;
            for (std::uint32_t index = 0; index < slotCount; ++index) {
                Slot &slot = m_slots[index];
                if (slot.counted) {
                    slot.visibilityOffset = m_visibilityUsed;
                    m_visibilityUsed += slot.groups;
                }
                markInstance(index);
            }
            writeInstances(false);
            result.clearState = true;
        }
        if (!m_history || m_history->size() < slotCount * sizeof(std::uint32_t)) {
            retire(m_history, frame);
            const std::uint32_t capacity = std::max(kInitialSlots, roundUpPow2(slotCount * 2));
            m_history = makeStateBuffer(m_device, capacity * sizeof(std::uint32_t), "instance visibility");
            result.clearState = true;
        }
        if (m_stateUndefined) {
            result.clearState = true;
            m_stateUndefined = false;
        }
    }

    // ---- Materials to rewrite --------------------------------------------------------------------------
    if (!m_materialDirty.empty()) {
        const UploadArena::Allocation alloc = arena.allocate(m_materialDirty.size() * sizeof(MaterialUpdate), 16);
        auto *out = static_cast<std::byte *>(alloc.data);
        for (std::uint32_t index : m_materialDirty) {
            Slot &slot = m_slots[index];
            MaterialUpdate update;
            update.material = m_slotMaterials[index].data;
            update.slot = index;
            std::memcpy(out, &update, sizeof(update));
            out += sizeof(update);
            slot.materialDirty = false;
        }
        result.materialUpdates = alloc.address;
        result.materialUpdateCount = static_cast<std::uint32_t>(m_materialDirty.size());
        m_materialDirty.clear();
    }

    // The mesh table: small, so written whole every frame. Free entries are never referenced.
    {
        const std::size_t count = std::max<std::size_t>(m_meshes.size(), 1);
        const UploadArena::Allocation alloc = arena.allocate(count * sizeof(GpuMesh), 16);
        auto *out = static_cast<std::byte *>(alloc.data);
        for (std::size_t index = 0; index < count; ++index) {
            const GpuMesh mesh = index < m_meshes.size() ? m_meshes[index].gpu : GpuMesh{};
            std::memcpy(out + index * sizeof(GpuMesh), &mesh, sizeof(GpuMesh));
        }
        result.meshes = alloc.address;
    }

    // ---- Blended objects: GpuDraws and work in back-to-front order, written here ----------------------
    if (m_blendedChanged) {
        m_blended.clear();
        for (std::uint32_t index = 0; index < slotCount; ++index) {
            const Slot &slot = m_slots[index];
            if (slot.occupied && slot.blended && slot.mesh != kNoMesh) {
                m_blended.push_back(index);
            }
        }
        m_blendedChanged = false;
    }
    struct Blended {
        float depth;
        std::uint32_t slot;
    };
    std::vector<Blended> order;
    order.reserve(m_blended.size());
    const vec3 forward = scene.camera().forward();
    for (std::uint32_t index : m_blended) {
        const Slot &slot = m_slots[index];
        const vec3 center = vec3(slot.model * vec4(m_meshes[slot.mesh].mesh->bounds().center(), 1.0f));
        order.push_back({glm::dot(center - view.cameraPosition, forward), index});
    }
    // Back to front, so each is composited over everything behind it. Ties keep slot order.
    std::stable_sort(order.begin(), order.end(), [](const Blended &a, const Blended &b) { return a.depth > b.depth; });
    std::vector<GpuDraw> blendDraws;
    std::vector<bool> blendCull;
    for (const Blended &item : order) {
        const Slot &slot = m_slots[item.slot];
        const GpuMesh &mesh = m_meshes[slot.mesh].gpu;
        const bool cull = cullsBackFaces(slot.doubleSided, slot.model);
        GpuDraw draw;
        draw.model = slot.model;
        draw.cameraPosition = glm::inverse(slot.model) * vec4(view.cameraPosition, 1.0f);
        draw.vertices = mesh.vertices;
        draw.primitives = m_meshlets ? mesh.meshlets : 0;
        draw.count = m_meshlets ? mesh.meshletCount : mesh.indexCount;
        draw.material = item.slot;
        draw.flags = cull ? kDrawConeCulling : 0u;
        blendDraws.push_back(draw);
        blendCull.push_back(cull);
    }
    if (!blendDraws.empty()) {
        result.blendDraws = arena.upload(blendDraws.data(), blendDraws.size() * sizeof(GpuDraw), 16).address;
    }
    for (std::size_t first = 0; first < blendDraws.size();) {
        const bool cull = blendCull[first];
        std::size_t end = first;
        while (end < blendDraws.size() && blendCull[end] == cull) ++end;
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
                const GpuMesh &mesh = m_meshes[m_slots[order[d].slot].mesh].gpu;
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

    result.instanceCount = slotCount;
    result.indexPool = meshes.indexPool();
    result.drawCount = m_device.drawIndirectCount();
    return result;
}

} // namespace phox::vk
