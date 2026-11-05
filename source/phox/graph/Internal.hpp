#ifndef PHOX_GRAPH_INTERNAL_HPP
#define PHOX_GRAPH_INTERNAL_HPP

#include "phox/graph/Pass.hpp"
#include "phox/graph/RenderGraph.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace phox::graph::detail {

struct TextureNode {
    rhi::TextureDesc desc{};
    std::string name;
    bool imported = false;
    rhi::TextureHandle handle; // imported: given; transient: set by compile() when a live pass uses it
    rhi::ResourceState before = rhi::ResourceState::Undefined;
    rhi::ResourceState after = rhi::ResourceState::Undefined;
};

struct BufferNode {
    rhi::BufferDesc desc{};
    std::string name;
    bool imported = false;
    rhi::BufferHandle handle;
    rhi::ResourceState before = rhi::ResourceState::Undefined;
    rhi::ResourceState after = rhi::ResourceState::Undefined;
};

struct TextureUse {
    uint32_t id;
    rhi::ResourceState state;
    rhi::TextureUsage usage;
    bool write;
};

struct BufferUse {
    uint32_t id;
    rhi::ResourceState state; // ignored when `host` is set
    rhi::BufferUsage usage;
    bool write;
    bool host; // HostWrite: the CPU wrote it before submit, so no barrier and no state tracking
};

struct ColorDecl {
    uint32_t id;
    bool clear;
    std::array<float, 4> value;
};

struct PassNode {
    std::string name;
    std::function<void(PassContext &)> execute;
    std::vector<TextureUse> textures; // includes colour and depth targets
    std::vector<BufferUse> buffers;
    std::vector<ColorDecl> colors;
    bool hasDepth = false;
    uint32_t depthId = 0;
    bool depthClear = true;
    float depthValue = 1.0f;
    bool sideEffect = false;

    // Filled in by compile().
    bool live = false;
    std::vector<rhi::TextureBarrier> textureBarriers;
    std::vector<rhi::BufferBarrier> bufferBarriers;
};

struct GraphData {
    std::vector<TextureNode> textures;
    std::vector<BufferNode> buffers;
    std::vector<PassNode> passes;
    std::vector<rhi::Feature> required;
    bool compiled = false;
    std::vector<rhi::TextureBarrier> finalTextureBarriers;
    std::vector<rhi::BufferBarrier> finalBufferBarriers;

    void clear() {
        textures.clear();
        buffers.clear();
        passes.clear();
        required.clear();
        compiled = false;
        finalTextureBarriers.clear();
        finalBufferBarriers.clear();
    }
};

std::string_view stateName(rhi::ResourceState);
bool isWriteState(rhi::ResourceState);
rhi::ResourceState stateOf(TextureAccess);
rhi::ResourceState stateOf(BufferAccess);
rhi::TextureUsage usageOf(TextureAccess);
rhi::BufferUsage usageOf(BufferAccess);

/// Fills `textureBarriers` / `bufferBarriers` of every live pass, in declaration order, plus the final
/// barriers that put imported resources into their declared `after` state. Throws GraphError when one
/// pass needs a resource in two different states.
void planBarriers(GraphData &graph);

} // namespace phox::graph::detail

#endif // PHOX_GRAPH_INTERNAL_HPP
