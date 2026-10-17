#ifndef PHOX_GRAPH_PASS_HPP
#define PHOX_GRAPH_PASS_HPP

#include "phox/graph/ResourceHandle.hpp"
#include "phox/rhi/CommandList.hpp"
#include "phox/rhi/Device.hpp"

#include <array>
#include <cstddef>

namespace phox::graph {

namespace detail {
struct GraphData;
struct PassNode;
} // namespace detail

enum class TextureAccess { ShaderRead, ShaderWrite, TransferSrc, TransferDst };
enum class BufferAccess { Vertex, Index, Uniform, ShaderRead, ShaderWrite, TransferSrc, TransferDst, HostWrite };

/// Declares what one pass touches. Handed to the `setup` callback of GraphBuilder::addPass.
class PassBuilder {
  public:
    void colorTarget(GraphTexture, bool clear = true, std::array<float, 4> clearColor = {0, 0, 0, 1});
    void depthTarget(GraphTexture, bool clear = true, float clearDepth = 1.0f);
    void read(GraphTexture, TextureAccess);
    void write(GraphTexture, TextureAccess);
    void read(GraphBuffer, BufferAccess);
    void write(GraphBuffer, BufferAccess);
    void sideEffect(); // pass is never culled (readback, debug output)

  private:
    friend class GraphBuilder;
    PassBuilder(detail::GraphData &graph, detail::PassNode &pass) : m_graph(graph), m_pass(pass) {}

    detail::GraphData &m_graph;
    detail::PassNode &m_pass;
};

/// Handed to the `execute` callback of a pass while its commands are recorded.
class PassContext {
  public:
    rhi::CommandList &cmd;
    rhi::Device &device;
    rhi::TextureHandle texture(GraphTexture) const; // must: the pass declared it
    rhi::BufferHandle buffer(GraphBuffer) const;

  private:
    friend class RenderGraph;
    PassContext(rhi::CommandList &commands, rhi::Device &dev, const detail::GraphData &graph, size_t pass)
        : cmd(commands), device(dev), m_graph(graph), m_pass(pass) {}

    const detail::GraphData &m_graph;
    size_t m_pass;
};

} // namespace phox::graph

#endif // PHOX_GRAPH_PASS_HPP
