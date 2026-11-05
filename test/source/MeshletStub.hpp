#ifndef PHOX_TEST_MESHLET_STUB_HPP
#define PHOX_TEST_MESHLET_STUB_HPP

#include "phox/graph/Component.hpp"

#include <string_view>

namespace phox::test {

// Not a real technique. It exists to prove that a component requiring a GPU feature is rejected at
// compile() time on devices without that feature, and accepted on devices that have it.
struct MeshletStub {
    struct In {};
    struct Out {};

    Out setup(graph::GraphBuilder &builder, const In &) {
        builder.require(rhi::Feature::MeshShader);
        builder.addPass(
            "meshlet", [](graph::PassBuilder &pass) { pass.sideEffect(); }, [](graph::PassContext &) {});
        return {};
    }
    std::string_view name() const { return "MeshletStub"; }
};

static_assert(graph::Component<MeshletStub>);

} // namespace phox::test

#endif // PHOX_TEST_MESHLET_STUB_HPP
