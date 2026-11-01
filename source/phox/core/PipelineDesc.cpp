#include "phox/pipeline/PipelineDesc.hpp"

#include <functional>

namespace phox {

bool PipelineDesc::operator==(const PipelineDesc &other) const {
    return vertexShader == other.vertexShader && taskShader == other.taskShader && meshShader == other.meshShader &&
           fragmentShader == other.fragmentShader &&
           vertexLayout == other.vertexLayout && topology == other.topology && raster == other.raster &&
           depth == other.depth && blend == other.blend && colorFormats == other.colorFormats &&
           depthFormat == other.depthFormat && pushConstants == other.pushConstants && setLayouts == other.setLayouts;
}

namespace {

void combine(std::size_t &seed, std::size_t value) { seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2); }

template <class E> std::size_t enumHash(E value) { return std::hash<unsigned>{}(static_cast<unsigned>(value)); }

} // namespace

std::size_t PipelineDescHash::operator()(const PipelineDesc &desc) const {
    std::size_t seed = 0;
    combine(seed, std::hash<std::string>{}(desc.vertexShader));
    combine(seed, std::hash<std::string>{}(desc.taskShader));
    combine(seed, std::hash<std::string>{}(desc.meshShader));
    combine(seed, std::hash<std::string>{}(desc.fragmentShader));
    for (const VertexAttribute &attribute : desc.vertexLayout.attributes()) {
        combine(seed, enumHash(attribute.semantic));
        combine(seed, enumHash(attribute.format));
        combine(seed, attribute.offset);
    }
    combine(seed, enumHash(desc.topology));
    combine(seed, enumHash(desc.raster.cull));
    combine(seed, enumHash(desc.raster.frontFace));
    combine(seed, (desc.depth.test ? 1u : 0u) | (desc.depth.write ? 2u : 0u));
    combine(seed, enumHash(desc.depth.compare));
    for (BlendMode mode : desc.blend) combine(seed, enumHash(mode));
    for (Format format : desc.colorFormats) combine(seed, enumHash(format));
    combine(seed, enumHash(desc.depthFormat));
    for (const PushConstantRange &range : desc.pushConstants) {
        combine(seed, range.stages);
        combine(seed, range.offset);
        combine(seed, range.size);
    }
    for (SetLayout layout : desc.setLayouts) combine(seed, enumHash(layout));
    return seed;
}

} // namespace phox
