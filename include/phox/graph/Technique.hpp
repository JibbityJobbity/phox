#ifndef PHOX_GRAPH_TECHNIQUE_HPP
#define PHOX_GRAPH_TECHNIQUE_HPP

#include "phox/graph/RenderGraph.hpp"
#include "phox/rhi/Capabilities.hpp"
#include "phox/rhi/Device.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

namespace phox::graph {

/// A whole frame built by chaining components. Applications that want a fallback pick another
/// technique themselves, using isSupported().
class Technique {
  public:
    virtual ~Technique() = default;
    virtual std::string_view name() const = 0;
    virtual std::vector<rhi::Feature> requirements() const = 0; // union of its components' requirements
    bool isSupported(const rhi::Capabilities &caps) const {    // all requirements present
        const std::vector<rhi::Feature> needed = requirements();
        return std::all_of(needed.begin(), needed.end(), [&](rhi::Feature f) { return caps.has(f); });
    }
    virtual void init(rhi::Device &) = 0;
    virtual void shutdown(rhi::Device &) = 0;
    virtual void build(GraphBuilder &) = 0; // adds this frame's passes; per-frame inputs set via the subclass
};

} // namespace phox::graph

#endif // PHOX_GRAPH_TECHNIQUE_HPP
