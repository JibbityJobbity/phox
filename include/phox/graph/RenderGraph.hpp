#ifndef PHOX_GRAPH_RENDER_GRAPH_HPP
#define PHOX_GRAPH_RENDER_GRAPH_HPP

#include "phox/graph/Pass.hpp"
#include "phox/graph/ResourceHandle.hpp"
#include "phox/rhi/Capabilities.hpp"
#include "phox/rhi/Device.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace phox::graph {

class GraphError : public std::runtime_error {
  public:
    std::vector<rhi::Feature> missingFeatures; // empty for non-capability errors

    explicit GraphError(const std::string &message, std::vector<rhi::Feature> missing = {})
        : std::runtime_error(message), missingFeatures(std::move(missing)) {}
};

class GraphBuilder {
  public:
    GraphTexture createTexture(const rhi::TextureDesc &); // transient; desc.usage is IGNORED, derived from accesses
    GraphBuffer createBuffer(const rhi::BufferDesc &);    // transient; same rule for usage
    GraphTexture importTexture(rhi::TextureHandle, rhi::ResourceState before, rhi::ResourceState after);
    GraphBuffer importBuffer(rhi::BufferHandle, rhi::ResourceState before, rhi::ResourceState after);
    void require(rhi::Feature);
    void addPass(std::string name,
                 std::function<void(PassBuilder &)> setup,     // runs immediately inside addPass
                 std::function<void(PassContext &)> execute);  // runs during execute()

  private:
    friend class RenderGraph;
    GraphBuilder() = default;

    rhi::Device *m_device = nullptr;
    detail::GraphData *m_graph = nullptr;
};

class RenderGraph {
  public:
    explicit RenderGraph(rhi::Device &); // owns the transient pool; keep one per device, reuse every frame
    ~RenderGraph();
    RenderGraph(const RenderGraph &) = delete;
    RenderGraph &operator=(const RenderGraph &) = delete;

    GraphBuilder &begin();              // resets declarations from the previous frame
    void compile();                     // throws GraphError
    rhi::SubmitTicket execute();        // records one CommandList, submits once
    rhi::BufferHandle resolve(GraphBuffer) const;    // valid after compile() until the next begin()
    rhi::TextureHandle resolve(GraphTexture) const;  // invalid handle if only culled passes use it
    std::string dump() const;           // passes in order, culled flag, barriers; used by tests

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace phox::graph

#endif // PHOX_GRAPH_RENDER_GRAPH_HPP
