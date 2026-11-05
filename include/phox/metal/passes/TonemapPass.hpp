#ifndef PHOX_METAL_PASSES_TONEMAP_PASS_HPP
#define PHOX_METAL_PASSES_TONEMAP_PASS_HPP

#include "phox/RenderGraph.hpp"
#include "phox/Scene.hpp"
#include "phox/pipeline/PipelineDesc.hpp"

namespace phox::mtl {

class PipelineCache;

/// The pipeline the tonemap pass uses to write a target of format @p target.
PipelineDesc tonemapPipelineDesc(Format target);

/// Maps @p hdr to the display range with @p mapping and writes every pixel of @p target, which must be
/// the same size (it may be a drawable's texture). Returns the target's new version.
ImageHandle addTonemapPass(RenderGraph &graph, ImageHandle hdr, ImageHandle target, const ToneMapping &mapping,
                           PipelineCache &pipelines);

} // namespace phox::mtl

#endif // PHOX_METAL_PASSES_TONEMAP_PASS_HPP
