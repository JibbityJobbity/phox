#ifndef PHOX_GRAPH_COMPONENT_HPP
#define PHOX_GRAPH_COMPONENT_HPP

#include "phox/graph/RenderGraph.hpp"

#include <concepts>
#include <string_view>

namespace phox::graph {

/// A reusable piece of a graph with typed inputs and outputs.
///
/// `In` and `Out` are plain structs of GraphTexture, GraphBuffer, persistent rhi:: handles and plain
/// values. A component may hold persistent rhi:: objects (pipelines, shaders): it creates them in an
/// `init(rhi::Device&)` method and frees them in `shutdown(rhi::Device&)`, never inside `setup()`.
/// A component never inspects device capabilities itself; it calls `b.require()` and lets compile fail.
template <class C>
concept Component = requires(C c, GraphBuilder &b, const typename C::In &in) {
    typename C::In;
    typename C::Out;
    { c.setup(b, in) } -> std::same_as<typename C::Out>; // must: call b.require() for each needed feature,
                                                         // then add 1+ passes
    { c.name() } -> std::convertible_to<std::string_view>;
};

} // namespace phox::graph

#endif // PHOX_GRAPH_COMPONENT_HPP
