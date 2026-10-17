#ifndef PHOX_RENDER_PIPELINE_HPP
#define PHOX_RENDER_PIPELINE_HPP

#include "phox/VertexLayout.hpp"

#include <string>

namespace phox {

class RenderPipeline {
public:
    RenderPipeline(std::string name, VertexLayout layout)
        : m_name(std::move(name)), m_vertexLayout(std::move(layout)) {}

    const std::string& name() const { return m_name; }
    const VertexLayout& vertexLayout() const { return m_vertexLayout; }

private:
    std::string m_name;
    VertexLayout m_vertexLayout;
};

} // namespace phox

#endif // PHOX_RENDER_PIPELINE_HPP
