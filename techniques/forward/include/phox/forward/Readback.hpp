#ifndef PHOX_FORWARD_READBACK_HPP
#define PHOX_FORWARD_READBACK_HPP

#include "phox/graph/Component.hpp"

#include <string_view>

namespace phox::forward {

/// Copies a texture into a CPU-readable buffer (tightly packed rows, mip 0). The buffer is sized from
/// the texture's declared format and extent. The pass has a side effect, so it is never culled.
/// Supported formats are the colour formats of rhi::Format; depth formats throw std::invalid_argument.
class Readback {
  public:
    struct In {
        graph::GraphTexture src;
    };
    struct Out {
        graph::GraphBuffer pixels;
    };

    Out setup(graph::GraphBuilder &builder, const In &in);
    std::string_view name() const { return "Readback"; }
};

static_assert(graph::Component<Readback>);

} // namespace phox::forward

#endif // PHOX_FORWARD_READBACK_HPP
