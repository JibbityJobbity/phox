#ifndef PHOX_VULKAN_PASSES_DEPTH_PYRAMID_PASS_HPP
#define PHOX_VULKAN_PASSES_DEPTH_PYRAMID_PASS_HPP

#include "phox/RenderGraph.hpp"
#include "phox/pipeline/PipelineDesc.hpp"

namespace phox::vk {

class PipelineCache;

/// The compute pipeline that builds one mip of a depth pyramid.
PipelineDesc depthPyramidPipelineDesc();

/// Builds a hierarchical depth buffer from @p depth (D32Float) for occlusion tests: an R32Float image
/// whose mip 0 is depthPyramidExtent() of the graph's output extent, each texel the farthest depth of
/// the depth pixels it covers, and whose further mips halve it down to 1x1, keeping the farthest of 2 x 2.
/// One compute pass; it orders its mips with barriers of its own. Returns the pyramid.
ImageHandle addDepthPyramidPass(RenderGraph &graph, ImageHandle depth, PipelineCache &pipelines);

} // namespace phox::vk

#endif // PHOX_VULKAN_PASSES_DEPTH_PYRAMID_PASS_HPP
