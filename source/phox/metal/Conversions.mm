#include "phox/metal/Conversions.hpp"

#include <stdexcept>

namespace phox::mtl {

MTLPixelFormat toMtl(Format format) {
    switch (format) {
    case Format::RGBA8Unorm: return MTLPixelFormatRGBA8Unorm;
    case Format::BGRA8Unorm: return MTLPixelFormatBGRA8Unorm;
    case Format::RGBA8Srgb: return MTLPixelFormatRGBA8Unorm_sRGB;
    case Format::BGRA8Srgb: return MTLPixelFormatBGRA8Unorm_sRGB;
    case Format::RGBA16Float: return MTLPixelFormatRGBA16Float;
    case Format::R32Float: return MTLPixelFormatR32Float;
    case Format::D32Float: return MTLPixelFormatDepth32Float;
    // Not on Apple silicon (MTLDevice.depth24Stencil8PixelFormatSupported); the renderer uses D32Float.
    case Format::D24UnormS8: return MTLPixelFormatDepth24Unorm_Stencil8;
    case Format::Undefined: break;
    }
    throw std::invalid_argument("Format::Undefined has no Metal pixel format");
}

Format fromMtl(MTLPixelFormat format) {
    switch (format) {
    case MTLPixelFormatRGBA8Unorm: return Format::RGBA8Unorm;
    case MTLPixelFormatBGRA8Unorm: return Format::BGRA8Unorm;
    case MTLPixelFormatRGBA8Unorm_sRGB: return Format::RGBA8Srgb;
    case MTLPixelFormatBGRA8Unorm_sRGB: return Format::BGRA8Srgb;
    case MTLPixelFormatRGBA16Float: return Format::RGBA16Float;
    case MTLPixelFormatR32Float: return Format::R32Float;
    case MTLPixelFormatDepth32Float: return Format::D32Float;
    case MTLPixelFormatDepth24Unorm_Stencil8: return Format::D24UnormS8;
    default: return Format::Undefined;
    }
}

MTLTextureUsage textureUsage(ImageUsageMask usage) {
    MTLTextureUsage result = MTLTextureUsageUnknown;
    const auto has = [&](ImageUsage u) { return (usage & usageBit(u)) != 0; };
    if (has(ImageUsage::ColorAttachment) || has(ImageUsage::DepthAttachment) || has(ImageUsage::DepthReadOnly)) {
        result |= MTLTextureUsageRenderTarget;
    }
    if (has(ImageUsage::Sampled) || has(ImageUsage::DepthReadOnly) || has(ImageUsage::StorageRead) ||
        has(ImageUsage::StorageWrite)) {
        result |= MTLTextureUsageShaderRead;
    }
    if (has(ImageUsage::StorageWrite)) {
        result |= MTLTextureUsageShaderWrite;
    }
    return result;
}

MTLVertexFormat toMtl(VertexAttributeFormat format) {
    switch (format) {
    case VertexAttributeFormat::Float32: return MTLVertexFormatFloat;
    case VertexAttributeFormat::Float32x2: return MTLVertexFormatFloat2;
    case VertexAttributeFormat::Float32x3: return MTLVertexFormatFloat3;
    case VertexAttributeFormat::Float32x4: return MTLVertexFormatFloat4;
    case VertexAttributeFormat::UInt32: return MTLVertexFormatUInt;
    }
    throw std::invalid_argument("unknown VertexAttributeFormat");
}

MTLPrimitiveType toMtl(Topology topology) {
    switch (topology) {
    case Topology::TriangleList: return MTLPrimitiveTypeTriangle;
    case Topology::TriangleStrip: return MTLPrimitiveTypeTriangleStrip;
    case Topology::LineList: return MTLPrimitiveTypeLine;
    case Topology::PointList: return MTLPrimitiveTypePoint;
    }
    throw std::invalid_argument("unknown Topology");
}

MTLPrimitiveTopologyClass topologyClass(Topology topology) {
    switch (topology) {
    case Topology::TriangleList:
    case Topology::TriangleStrip: return MTLPrimitiveTopologyClassTriangle;
    case Topology::LineList: return MTLPrimitiveTopologyClassLine;
    case Topology::PointList: return MTLPrimitiveTopologyClassPoint;
    }
    throw std::invalid_argument("unknown Topology");
}

MTLCullMode toMtl(CullMode mode) {
    switch (mode) {
    case CullMode::None: return MTLCullModeNone;
    case CullMode::Front: return MTLCullModeFront;
    case CullMode::Back: return MTLCullModeBack;
    }
    throw std::invalid_argument("unknown CullMode");
}

MTLWinding toMtl(FrontFace face) {
    return face == FrontFace::CounterClockwise ? MTLWindingCounterClockwise : MTLWindingClockwise;
}

MTLCompareFunction toMtl(CompareOp op) {
    switch (op) {
    case CompareOp::Never: return MTLCompareFunctionNever;
    case CompareOp::Less: return MTLCompareFunctionLess;
    case CompareOp::Equal: return MTLCompareFunctionEqual;
    case CompareOp::LessOrEqual: return MTLCompareFunctionLessEqual;
    case CompareOp::Greater: return MTLCompareFunctionGreater;
    case CompareOp::NotEqual: return MTLCompareFunctionNotEqual;
    case CompareOp::GreaterOrEqual: return MTLCompareFunctionGreaterEqual;
    case CompareOp::Always: return MTLCompareFunctionAlways;
    }
    throw std::invalid_argument("unknown CompareOp");
}

MTLLoadAction toMtl(LoadOp op) {
    switch (op) {
    case LoadOp::Load: return MTLLoadActionLoad;
    case LoadOp::Clear: return MTLLoadActionClear;
    case LoadOp::DontCare: return MTLLoadActionDontCare;
    }
    throw std::invalid_argument("unknown LoadOp");
}

MTLStoreAction toMtl(StoreOp op) { return op == StoreOp::Store ? MTLStoreActionStore : MTLStoreActionDontCare; }

} // namespace phox::mtl
