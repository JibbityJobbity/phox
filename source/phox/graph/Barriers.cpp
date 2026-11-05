#include "Internal.hpp"

namespace phox::graph::detail {

std::string_view stateName(rhi::ResourceState state) {
    switch (state) {
    case rhi::ResourceState::Undefined: return "Undefined";
    case rhi::ResourceState::ColorAttachment: return "ColorAttachment";
    case rhi::ResourceState::DepthWrite: return "DepthWrite";
    case rhi::ResourceState::DepthRead: return "DepthRead";
    case rhi::ResourceState::ShaderRead: return "ShaderRead";
    case rhi::ResourceState::ShaderWrite: return "ShaderWrite";
    case rhi::ResourceState::TransferSrc: return "TransferSrc";
    case rhi::ResourceState::TransferDst: return "TransferDst";
    case rhi::ResourceState::VertexOrIndex: return "VertexOrIndex";
    case rhi::ResourceState::Uniform: return "Uniform";
    case rhi::ResourceState::Present: return "Present";
    }
    return "?";
}

// States whose accesses write. Two consecutive accesses in the same write state still need a barrier,
// because the second one must wait for (and see) the first one's writes.
bool isWriteState(rhi::ResourceState state) {
    switch (state) {
    case rhi::ResourceState::ColorAttachment:
    case rhi::ResourceState::DepthWrite:
    case rhi::ResourceState::ShaderWrite:
    case rhi::ResourceState::TransferDst:
        return true;
    default:
        return false;
    }
}

// ---- access -> state / usage: the one table ----

rhi::ResourceState stateOf(TextureAccess access) {
    switch (access) {
    case TextureAccess::ShaderRead: return rhi::ResourceState::ShaderRead;
    case TextureAccess::ShaderWrite: return rhi::ResourceState::ShaderWrite;
    case TextureAccess::TransferSrc: return rhi::ResourceState::TransferSrc;
    case TextureAccess::TransferDst: return rhi::ResourceState::TransferDst;
    }
    return rhi::ResourceState::Undefined;
}

rhi::ResourceState stateOf(BufferAccess access) {
    switch (access) {
    case BufferAccess::Vertex:
    case BufferAccess::Index: return rhi::ResourceState::VertexOrIndex;
    case BufferAccess::Uniform: return rhi::ResourceState::Uniform;
    case BufferAccess::ShaderRead: return rhi::ResourceState::ShaderRead;
    case BufferAccess::ShaderWrite: return rhi::ResourceState::ShaderWrite;
    case BufferAccess::TransferSrc: return rhi::ResourceState::TransferSrc;
    case BufferAccess::TransferDst: return rhi::ResourceState::TransferDst;
    case BufferAccess::HostWrite: return rhi::ResourceState::Undefined; // no barrier
    }
    return rhi::ResourceState::Undefined;
}

rhi::TextureUsage usageOf(TextureAccess access) {
    switch (access) {
    case TextureAccess::ShaderRead: return rhi::TextureUsage::Sampled;
    case TextureAccess::ShaderWrite: return rhi::TextureUsage::Storage;
    case TextureAccess::TransferSrc: return rhi::TextureUsage::TransferSrc;
    case TextureAccess::TransferDst: return rhi::TextureUsage::TransferDst;
    }
    return rhi::TextureUsage{};
}

rhi::BufferUsage usageOf(BufferAccess access) {
    switch (access) {
    case BufferAccess::Vertex: return rhi::BufferUsage::Vertex;
    case BufferAccess::Index: return rhi::BufferUsage::Index;
    case BufferAccess::Uniform: return rhi::BufferUsage::Uniform;
    case BufferAccess::ShaderRead:
    case BufferAccess::ShaderWrite: return rhi::BufferUsage::Storage;
    case BufferAccess::TransferSrc: return rhi::BufferUsage::TransferSrc;
    case BufferAccess::TransferDst: return rhi::BufferUsage::TransferDst;
    case BufferAccess::HostWrite: return rhi::BufferUsage{};
    }
    return rhi::BufferUsage{};
}

namespace {

struct Tracked {
    rhi::ResourceState state = rhi::ResourceState::Undefined;
};

std::string label(const std::string &name, const char *kind, uint32_t id) {
    return name.empty() ? std::string(kind) + "#" + std::to_string(id) : name;
}

} // namespace

void planBarriers(GraphData &graph) {
    std::vector<Tracked> textures(graph.textures.size());
    std::vector<Tracked> buffers(graph.buffers.size());
    for (size_t i = 0; i < graph.textures.size(); ++i) {
        if (graph.textures[i].imported) {
            textures[i].state = graph.textures[i].before;
        }
    }
    for (size_t i = 0; i < graph.buffers.size(); ++i) {
        if (graph.buffers[i].imported) {
            buffers[i].state = graph.buffers[i].before;
        }
    }

    for (PassNode &pass : graph.passes) {
        pass.textureBarriers.clear();
        pass.bufferBarriers.clear();
        if (!pass.live) {
            continue;
        }

        // The state this pass needs each resource in; one pass cannot need two states of one resource.
        std::vector<std::pair<uint32_t, rhi::ResourceState>> textureStates;
        for (const TextureUse &use : pass.textures) {
            bool found = false;
            for (auto &entry : textureStates) {
                if (entry.first == use.id) {
                    if (entry.second != use.state) {
                        throw GraphError("pass '" + pass.name + "' uses texture '" +
                                         label(graph.textures[use.id].name, "texture", use.id) + "' as both " +
                                         std::string(stateName(entry.second)) + " and " + std::string(stateName(use.state)));
                    }
                    found = true;
                }
            }
            if (!found) {
                textureStates.emplace_back(use.id, use.state);
            }
        }
        std::vector<std::pair<uint32_t, rhi::ResourceState>> bufferStates;
        for (const BufferUse &use : pass.buffers) {
            if (use.host) {
                continue;
            }
            bool found = false;
            for (auto &entry : bufferStates) {
                if (entry.first == use.id) {
                    if (entry.second != use.state) {
                        throw GraphError("pass '" + pass.name + "' uses buffer '" +
                                         label(graph.buffers[use.id].name, "buffer", use.id) + "' as both " +
                                         std::string(stateName(entry.second)) + " and " + std::string(stateName(use.state)));
                    }
                    found = true;
                }
            }
            if (!found) {
                bufferStates.emplace_back(use.id, use.state);
            }
        }

        for (const auto &[id, state] : textureStates) {
            Tracked &tracked = textures[id];
            if (tracked.state != state || isWriteState(state)) {
                pass.textureBarriers.push_back({graph.textures[id].handle, tracked.state, state});
            }
            tracked.state = state;
        }
        for (const auto &[id, state] : bufferStates) {
            Tracked &tracked = buffers[id];
            if (tracked.state != state || isWriteState(state)) {
                pass.bufferBarriers.push_back({graph.buffers[id].handle, tracked.state, state});
            }
            tracked.state = state;
        }
    }

    graph.finalTextureBarriers.clear();
    graph.finalBufferBarriers.clear();
    for (size_t i = 0; i < graph.textures.size(); ++i) {
        const TextureNode &node = graph.textures[i];
        // `after == Undefined` means the caller does not care what state the resource is left in.
        if (node.imported && node.after != rhi::ResourceState::Undefined && textures[i].state != node.after) {
            graph.finalTextureBarriers.push_back({node.handle, textures[i].state, node.after});
        }
    }
    for (size_t i = 0; i < graph.buffers.size(); ++i) {
        const BufferNode &node = graph.buffers[i];
        if (node.imported && node.after != rhi::ResourceState::Undefined && buffers[i].state != node.after) {
            graph.finalBufferBarriers.push_back({node.handle, buffers[i].state, node.after});
        }
    }
}

} // namespace phox::graph::detail
