#ifndef PHOX_PIPELINE_HPP
#define PHOX_PIPELINE_HPP

#include "phox/pipeline/PipelineDesc.hpp"

#include <utility>

namespace phox {

/// A compiled pipeline. Backends derive from this and add their handles.
class Pipeline {
public:
    explicit Pipeline(PipelineDesc desc) : m_desc(std::move(desc)) {}
    virtual ~Pipeline() = default;
    Pipeline(const Pipeline &) = delete;
    Pipeline &operator=(const Pipeline &) = delete;

    const PipelineDesc &desc() const { return m_desc; }

protected:
    PipelineDesc m_desc;
};

}

#endif // PHOX_PIPELINE_HPP
