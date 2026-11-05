#ifndef PHOX_GRAPH_RESOURCE_HANDLE_HPP
#define PHOX_GRAPH_RESOURCE_HANDLE_HPP

#include <cstdint>

namespace phox::graph {

/// A texture declared in the current frame's graph. Only meaningful for the RenderGraph that made it,
/// and only until the next RenderGraph::begin().
struct GraphTexture {
    uint32_t id = UINT32_MAX;
    bool valid() const { return id != UINT32_MAX; }
    bool operator==(const GraphTexture &) const = default;
};

/// A buffer declared in the current frame's graph. Same lifetime rules as GraphTexture.
struct GraphBuffer {
    uint32_t id = UINT32_MAX;
    bool valid() const { return id != UINT32_MAX; }
    bool operator==(const GraphBuffer &) const = default;
};

} // namespace phox::graph

#endif // PHOX_GRAPH_RESOURCE_HANDLE_HPP
