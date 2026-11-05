#ifndef PHOX_METAL_CONVERSIONS_HPP
#define PHOX_METAL_CONVERSIONS_HPP

#include "phox/RenderGraph.hpp"
#include "phox/Resources.hpp"
#include "phox/VertexLayout.hpp"
#include "phox/metal/MetalCommon.hpp"
#include "phox/pipeline/PipelineDesc.hpp"

/// Translations from Phox's backend-free vocabulary to Metal's.
namespace phox::mtl {

/// @throws std::invalid_argument for Format::Undefined
MTLPixelFormat toMtl(Format format);
/// Format::Undefined for formats Phox has no name for.
Format fromMtl(MTLPixelFormat format);
/// The texture usage a set of ImageUsages needs. Transfers need no flag in Metal.
MTLTextureUsage textureUsage(ImageUsageMask usage);
MTLVertexFormat toMtl(VertexAttributeFormat format);
MTLPrimitiveType toMtl(Topology topology);
MTLPrimitiveTopologyClass topologyClass(Topology topology);
MTLCullMode toMtl(CullMode mode);
MTLWinding toMtl(FrontFace face);
MTLCompareFunction toMtl(CompareOp op);
MTLLoadAction toMtl(LoadOp op);
MTLStoreAction toMtl(StoreOp op);

} // namespace phox::mtl

#endif // PHOX_METAL_CONVERSIONS_HPP
