#ifndef PHOX_VERTEX_LAYOUT_HPP
#define PHOX_VERTEX_LAYOUT_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace phox {

enum class VertexAttributeSemantic {
    Position,
    Normal,
    Tangent,
    TexCoord0,
    TexCoord1,
    Color0,
    BoneWeights,
    BoneIndices
};

enum class VertexAttributeFormat {
    Float32,
    Float32x2,
    Float32x3,
    Float32x4,
    UInt32
};

struct VertexAttribute {
    VertexAttributeSemantic semantic = VertexAttributeSemantic::Position;
    VertexAttributeFormat format = VertexAttributeFormat::Float32x3;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

class VertexLayout {
public:
    static VertexLayout positionOnly() {
        return VertexLayout{{VertexAttribute{VertexAttributeSemantic::Position, VertexAttributeFormat::Float32x3, 0, 12}}};
    }

    static VertexLayout positionNormalTexcoord() {
        return VertexLayout{{
            VertexAttribute{VertexAttributeSemantic::Position, VertexAttributeFormat::Float32x3, 0, 12},
            VertexAttribute{VertexAttributeSemantic::Normal, VertexAttributeFormat::Float32x3, 12, 12},
            VertexAttribute{VertexAttributeSemantic::TexCoord0, VertexAttributeFormat::Float32x2, 24, 8},
        }};
    }

    VertexLayout() = default;
    explicit VertexLayout(std::vector<VertexAttribute> attributes)
        : m_attributes(std::move(attributes)) {}

    const std::vector<VertexAttribute>& attributes() const { return m_attributes; }
    bool hasAttribute(VertexAttributeSemantic semantic) const {
        for (const auto &attribute : m_attributes) {
            if (attribute.semantic == semantic) {
                return true;
            }
        }
        return false;
    }

    std::uint32_t stride() const {
        std::uint32_t size = 0;
        for (const auto &attribute : m_attributes) {
            size += attribute.size;
        }
        return size;
    }

private:
    std::vector<VertexAttribute> m_attributes;
};

} // namespace phox

#endif // PHOX_VERTEX_LAYOUT_HPP
