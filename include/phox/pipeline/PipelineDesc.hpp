#ifndef PHOX_PIPELINE_DESC_HPP
#define PHOX_PIPELINE_DESC_HPP

#include "phox/Resources.hpp"
#include "phox/VertexLayout.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace phox {

enum class Topology : std::uint8_t { TriangleList, TriangleStrip, LineList, PointList };
enum class CullMode : std::uint8_t { None, Front, Back };
enum class FrontFace : std::uint8_t { CounterClockwise, Clockwise };
enum class CompareOp : std::uint8_t { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always };
enum class BlendMode : std::uint8_t {
    Opaque,   ///< no blending
    Alpha,    ///< src * a + dst * (1 - a)
    Additive, ///< src + dst
};

/// Shader stages, as a mask.
using ShaderStages = std::uint32_t;
inline constexpr ShaderStages kVertexStage = 1u << 0;
inline constexpr ShaderStages kFragmentStage = 1u << 1;
inline constexpr ShaderStages kComputeStage = 1u << 2;
inline constexpr ShaderStages kTaskStage = 1u << 3;
inline constexpr ShaderStages kMeshStage = 1u << 4;

struct RasterState {
    CullMode cull = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;
    friend bool operator==(const RasterState &, const RasterState &) = default;
};

struct DepthState {
    bool test = false;
    bool write = false;
    CompareOp compare = CompareOp::Less;
    friend bool operator==(const DepthState &, const DepthState &) = default;
};

struct PushConstantRange {
    ShaderStages stages = 0;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    friend bool operator==(const PushConstantRange &, const PushConstantRange &) = default;
};

/// The descriptor set layouts the renderer defines. A pipeline lists the ones it uses, in set order.
enum class SetLayout : std::uint8_t {
    Frame,   ///< binding 0: the per-frame uniform buffer (camera, light), vertex, fragment, task and mesh stages
    Texture, ///< binding 0: one texture with a linear clamp-to-edge sampler, fragment stage
};

/// Everything that identifies a graphics pipeline, in backend-free terms. Equal descriptions (ignoring
/// the name) produce the same pipeline, so a backend can cache pipelines by description.
///
/// A pipeline either has a vertex shader (with vertex input and topology) or a mesh shader, optionally
/// fed by a task shader; mesh pipelines have no vertex input, and their topology is set by the shader.
struct PipelineDesc {
    std::string name;           ///< for debuggers; not part of the pipeline's identity
    std::string vertexShader;   ///< a shader in the shader directory, e.g. "forward.vert"
    std::string taskShader;     ///< mesh pipelines only; empty: the mesh shader is dispatched directly
    std::string meshShader;     ///< set instead of vertexShader for a mesh pipeline, e.g. "forward.mesh"
    std::string fragmentShader; ///< empty: no fragment stage (e.g. depth only)
    VertexLayout vertexLayout;  ///< empty: no vertex input (e.g. a full-screen triangle)
    Topology topology = Topology::TriangleList;
    RasterState raster;
    DepthState depth;
    std::vector<BlendMode> blend; ///< per colour attachment; missing entries are Opaque
    std::vector<Format> colorFormats;
    Format depthFormat = Format::Undefined;
    std::vector<PushConstantRange> pushConstants;
    std::vector<SetLayout> setLayouts;

    bool operator==(const PipelineDesc &other) const;
};

struct PipelineDescHash {
    std::size_t operator()(const PipelineDesc &desc) const;
};

} // namespace phox

#endif // PHOX_PIPELINE_DESC_HPP
