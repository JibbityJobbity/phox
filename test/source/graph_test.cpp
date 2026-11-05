#include "Check.hpp"
#include "MeshletStub.hpp"

#include "phox/graph/RenderGraph.hpp"
#include "phox/graph/Technique.hpp"
#include "phox/null/NullDevice.hpp"

#include <algorithm>
#include <bitset>
#include <cstring>
#include <string>
#include <vector>

using namespace phox;
using graph::BufferAccess;
using graph::GraphBuffer;
using graph::GraphError;
using graph::GraphTexture;
using graph::PassBuilder;
using graph::PassContext;
using graph::TextureAccess;

namespace {

constexpr auto npos = std::string::npos;

rhi::TextureDesc texture(const char *name, uint32_t size = 64) {
    // The usage is ignored for transient textures: the graph derives it from the accesses.
    return {size, size, 1, rhi::Format::RGBA8Unorm, rhi::TextureUsage::Sampled, name};
}

bool has(rhi::TextureUsage set, rhi::TextureUsage bit) { return (set & bit) != rhi::TextureUsage{}; }

// The text of the dump belonging to one pass (or to "final").
std::string passBlock(const std::string &dump, const std::string &name) {
    const std::string header = name == "final" ? "final\n" : "\"" + name + "\" ";
    const size_t start = dump.find(header);
    CHECK(start != npos);
    const size_t lineEnd = dump.find('\n', start);
    size_t end = dump.find("\npass ", lineEnd);
    const size_t finalPos = dump.find("\nfinal\n", lineEnd);
    end = std::min(end, finalPos);
    return dump.substr(start, end == npos ? npos : end - start);
}

std::bitset<32> featureSet(std::initializer_list<rhi::Feature> features) {
    std::bitset<32> set;
    for (const rhi::Feature f : features) {
        set.set(static_cast<size_t>(f));
    }
    return set;
}

void testBarriersBetweenPasses() {
    null::NullDevice device;
    graph::RenderGraph renderGraph(device);
    auto &b = renderGraph.begin();
    const GraphTexture t = b.createTexture(texture("T"));
    b.addPass("A", [&](PassBuilder &p) { p.colorTarget(t); }, [](PassContext &c) { c.cmd.draw(3); });
    b.addPass("B", [&](PassBuilder &p) { p.read(t, TextureAccess::ShaderRead); p.sideEffect(); },
              [](PassContext &c) { c.cmd.dispatch(1, 1, 1); });
    renderGraph.compile();

    const std::string dump = renderGraph.dump();
    CHECK(passBlock(dump, "A").find("Undefined -> ColorAttachment") != npos);
    CHECK(passBlock(dump, "B").find("ColorAttachment -> ShaderRead") != npos);
    CHECK(passBlock(dump, "B").find("Undefined") == npos);

    // The transient's usage is derived from what the passes do with it.
    const rhi::TextureDesc &created = device.describe(renderGraph.resolve(t));
    CHECK(has(created.usage, rhi::TextureUsage::ColorAttachment) && has(created.usage, rhi::TextureUsage::Sampled));
    CHECK(!has(created.usage, rhi::TextureUsage::Storage));

    renderGraph.execute();
    const std::vector<std::string> expected{"barrier",        "beginDebugLabel:A", "beginRendering", "draw",
                                            "endRendering",   "endDebugLabel",     "barrier",        "beginDebugLabel:B",
                                            "dispatch",       "endDebugLabel"};
    CHECK(device.lastRecording() == expected);
}

void testCulling() {
    null::NullDevice device;
    graph::RenderGraph renderGraph(device);
    auto &b = renderGraph.begin();
    const GraphTexture unused = b.createTexture(texture("unused"));
    b.addPass("X", [&](PassBuilder &p) { p.colorTarget(unused); }, [](PassContext &c) { c.cmd.draw(3); });
    b.addPass("Y", [](PassBuilder &p) { p.sideEffect(); }, [](PassContext &c) { c.cmd.dispatch(1, 1, 1); });
    renderGraph.compile();

    CHECK(passBlock(renderGraph.dump(), "X").find("culled") != npos);
    CHECK(passBlock(renderGraph.dump(), "Y").find("live") != npos);
    CHECK(!renderGraph.resolve(unused).valid()); // only a culled pass uses it, so it was never created
    renderGraph.execute();
    const auto &recording = device.lastRecording();
    CHECK(std::find(recording.begin(), recording.end(), "draw") == recording.end());
    CHECK(std::find(recording.begin(), recording.end(), "dispatch") != recording.end());
}

void testCullingFollowsDependencies() {
    null::NullDevice device;
    const rhi::BufferHandle sink = device.createBuffer({64, rhi::BufferUsage::Storage, rhi::MemoryKind::Readback, "sink"});
    graph::RenderGraph renderGraph(device);
    auto &b = renderGraph.begin();
    const GraphBuffer mid = b.createBuffer({64, rhi::BufferUsage{}, rhi::MemoryKind::GpuOnly, "mid"});
    const GraphBuffer imported = b.importBuffer(sink, rhi::ResourceState::Undefined, rhi::ResourceState::Undefined);
    const GraphBuffer orphan = b.createBuffer({64, rhi::BufferUsage{}, rhi::MemoryKind::GpuOnly, "orphan"});
    b.addPass("produce", [&](PassBuilder &p) { p.write(mid, BufferAccess::ShaderWrite); }, [](PassContext &) {});
    b.addPass("consume", [&](PassBuilder &p) { p.read(mid, BufferAccess::ShaderRead); p.write(imported, BufferAccess::ShaderWrite); },
              [](PassContext &) {});
    b.addPass("orphan", [&](PassBuilder &p) { p.write(orphan, BufferAccess::ShaderWrite); }, [](PassContext &) {});
    renderGraph.compile();

    const std::string dump = renderGraph.dump();
    CHECK(passBlock(dump, "produce").find("live") != npos);   // feeds a pass that writes an imported resource
    CHECK(passBlock(dump, "consume").find("live") != npos);   // writes an imported resource
    CHECK(passBlock(dump, "orphan").find("culled") != npos);  // writes something nobody needs
}

void testReadOfUnwrittenTransient() {
    null::NullDevice device;
    graph::RenderGraph renderGraph(device);
    auto &b = renderGraph.begin();
    const GraphTexture t = b.createTexture(texture("never-written"));
    b.addPass("reader", [&](PassBuilder &p) { p.read(t, TextureAccess::ShaderRead); p.sideEffect(); }, [](PassContext &) {});
    CHECK_THROWS_WITH(renderGraph.compile(), GraphError, "reader");
    CHECK_THROWS_WITH(renderGraph.compile(), GraphError, "no earlier pass writes");

    // Loading a target (clear = false) is a read too.
    auto &b2 = renderGraph.begin();
    const GraphTexture t2 = b2.createTexture(texture("loaded"));
    b2.addPass("loader", [&](PassBuilder &p) { p.colorTarget(t2, false); p.sideEffect(); }, [](PassContext &) {});
    CHECK_THROWS_WITH(renderGraph.compile(), GraphError, "loads texture");
}

void testCapabilityGating() {
    null::NullDevice device; // no features
    graph::RenderGraph renderGraph(device);
    phox::test::MeshletStub stub;

    stub.setup(renderGraph.begin(), {});
    bool thrown = false;
    try {
        renderGraph.compile();
    } catch (const GraphError &error) {
        thrown = true;
        CHECK(error.missingFeatures == std::vector<rhi::Feature>{rhi::Feature::MeshShader});
        CHECK(std::string(error.what()).find("MeshShader") != npos);
    }
    CHECK(thrown);

    device.setFeatures(featureSet({rhi::Feature::MeshShader}));
    stub.setup(renderGraph.begin(), {});
    renderGraph.compile();
    renderGraph.execute(); // does not throw

    // Every missing feature is reported, once each, in declaration order.
    device.setFeatures({});
    auto &b = renderGraph.begin();
    b.require(rhi::Feature::RayTracing);
    b.require(rhi::Feature::MeshShader);
    b.require(rhi::Feature::RayTracing);
    try {
        renderGraph.compile();
        CHECK(false);
    } catch (const GraphError &error) {
        CHECK((error.missingFeatures == std::vector<rhi::Feature>{rhi::Feature::RayTracing, rhi::Feature::MeshShader}));
    }
}

struct StubTechnique final : graph::Technique {
    std::string_view name() const override { return "stub"; }
    std::vector<rhi::Feature> requirements() const override { return {rhi::Feature::MeshShader, rhi::Feature::Atomic64}; }
    void init(rhi::Device &) override {}
    void shutdown(rhi::Device &) override {}
    void build(graph::GraphBuilder &) override {}
};

void testTechniqueSupport() {
    null::NullDevice device;
    StubTechnique technique;
    CHECK(!technique.isSupported(device.capabilities()));
    device.setFeatures(featureSet({rhi::Feature::MeshShader}));
    CHECK(!technique.isSupported(device.capabilities()));
    device.setFeatures(featureSet({rhi::Feature::MeshShader, rhi::Feature::Atomic64}));
    CHECK(technique.isSupported(device.capabilities()));
}

rhi::TextureHandle buildFrame(graph::RenderGraph &renderGraph, GraphTexture &out) {
    auto &b = renderGraph.begin();
    out = b.createTexture(texture("T"));
    const GraphTexture t = out;
    b.addPass("A", [t](PassBuilder &p) { p.colorTarget(t); }, [](PassContext &c) { c.cmd.draw(3); });
    b.addPass("B", [t](PassBuilder &p) { p.read(t, TextureAccess::ShaderRead); p.sideEffect(); }, [](PassContext &) {});
    renderGraph.compile();
    return renderGraph.resolve(out);
}

void emptyFrame(graph::RenderGraph &renderGraph) {
    renderGraph.begin();
    renderGraph.compile();
}

void testPoolReuse() {
    null::NullDevice device;
    graph::RenderGraph renderGraph(device);
    GraphTexture t;
    const rhi::TextureHandle first = buildFrame(renderGraph, t);
    CHECK(first.valid());
    renderGraph.execute();

    // The same frame again reuses the pooled texture: no new resource is created.
    CHECK(buildFrame(renderGraph, t) == first);

    // Two frames that do not use it keep it alive...
    emptyFrame(renderGraph);
    emptyFrame(renderGraph);
    CHECK(buildFrame(renderGraph, t) == first);

    // ...and the third unused frame retires it. The next frame gets a fresh resource.
    emptyFrame(renderGraph);
    emptyFrame(renderGraph);
    emptyFrame(renderGraph);
    const rhi::TextureHandle recreated = buildFrame(renderGraph, t);
    CHECK(recreated != first);
    CHECK(device.describe(recreated).width == 64);

    // A different size is a different pool entry.
    auto &b = renderGraph.begin();
    const GraphTexture big = b.createTexture(texture("big", 128));
    b.addPass("A", [big](PassBuilder &p) { p.colorTarget(big); }, [](PassContext &) {});
    b.addPass("B", [big](PassBuilder &p) { p.read(big, TextureAccess::ShaderRead); p.sideEffect(); }, [](PassContext &) {});
    renderGraph.compile();
    CHECK(device.describe(renderGraph.resolve(big)).width == 128);
}

void testImportedFinalState() {
    null::NullDevice device;
    const rhi::TextureHandle target = device.createTexture(
        {64, 64, 1, rhi::Format::RGBA8Unorm, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc, "target"});
    graph::RenderGraph renderGraph(device);
    auto &b = renderGraph.begin();
    const GraphTexture t = b.importTexture(target, rhi::ResourceState::Undefined, rhi::ResourceState::TransferSrc);
    b.addPass("draw", [t](PassBuilder &p) { p.colorTarget(t); }, [](PassContext &c) { c.cmd.draw(3); });
    renderGraph.compile();

    const std::string dump = renderGraph.dump();
    CHECK(passBlock(dump, "draw").find("Undefined -> ColorAttachment") != npos);
    CHECK(passBlock(dump, "final").find("ColorAttachment -> TransferSrc") != npos);
    CHECK(renderGraph.resolve(t) == target); // imports keep their own handle

    renderGraph.execute();
    CHECK(device.lastRecording().back() == "barrier");

    // `after = Undefined` means "leave it wherever it ends up": no final barrier.
    auto &b2 = renderGraph.begin();
    const GraphTexture t2 = b2.importTexture(target, rhi::ResourceState::Undefined, rhi::ResourceState::Undefined);
    b2.addPass("draw", [t2](PassBuilder &p) { p.colorTarget(t2); }, [](PassContext &) {});
    renderGraph.compile();
    CHECK(passBlock(renderGraph.dump(), "final").find("barrier") == npos);

    // An imported resource that no pass touches still ends up in its declared `after` state.
    auto &b3 = renderGraph.begin();
    b3.importTexture(target, rhi::ResourceState::ShaderRead, rhi::ResourceState::TransferSrc);
    renderGraph.compile();
    CHECK(passBlock(renderGraph.dump(), "final").find("ShaderRead -> TransferSrc") != npos);
}

void testWriteAfterWrite() {
    null::NullDevice device;
    const rhi::BufferHandle buffer = device.createBuffer({64, rhi::BufferUsage::Storage, rhi::MemoryKind::GpuOnly, "buf"});
    graph::RenderGraph renderGraph(device);
    auto &b = renderGraph.begin();
    const GraphBuffer buf = b.importBuffer(buffer, rhi::ResourceState::Undefined, rhi::ResourceState::Undefined);
    b.addPass("first", [buf](PassBuilder &p) { p.write(buf, BufferAccess::ShaderWrite); }, [](PassContext &) {});
    b.addPass("second", [buf](PassBuilder &p) { p.write(buf, BufferAccess::ShaderWrite); }, [](PassContext &) {});
    b.addPass("reader", [buf](PassBuilder &p) { p.read(buf, BufferAccess::ShaderRead); }, [](PassContext &) {});
    b.addPass("reader2", [buf](PassBuilder &p) { p.read(buf, BufferAccess::ShaderRead); }, [](PassContext &) {});
    renderGraph.compile();

    const std::string dump = renderGraph.dump();
    CHECK(passBlock(dump, "first").find("Undefined -> ShaderWrite") != npos);
    CHECK(passBlock(dump, "second").find("ShaderWrite -> ShaderWrite") != npos); // a write hazard: must wait
    // The readers do not write, and no live pass uses their result, so they are culled.
    CHECK(passBlock(dump, "reader").find("culled") != npos);
}

void testReadAfterReadNeedsNoBarrier() {
    null::NullDevice device;
    graph::RenderGraph renderGraph(device);
    auto &b = renderGraph.begin();
    const GraphTexture t = b.createTexture(texture("T"));
    b.addPass("write", [t](PassBuilder &p) { p.colorTarget(t); }, [](PassContext &) {});
    b.addPass("read1", [t](PassBuilder &p) { p.read(t, TextureAccess::ShaderRead); p.sideEffect(); }, [](PassContext &) {});
    b.addPass("read2", [t](PassBuilder &p) { p.read(t, TextureAccess::ShaderRead); p.sideEffect(); }, [](PassContext &) {});
    renderGraph.compile();
    const std::string dump = renderGraph.dump();
    CHECK(passBlock(dump, "read1").find("barrier") != npos);
    CHECK(passBlock(dump, "read2").find("barrier") == npos);
}

void testHostWrites() {
    null::NullDevice device;
    const rhi::TextureHandle target = device.createTexture(
        {64, 64, 1, rhi::Format::RGBA8Unorm, rhi::TextureUsage::ColorAttachment, "target"});
    graph::RenderGraph renderGraph(device);
    auto &b = renderGraph.begin();
    const GraphTexture out = b.importTexture(target, rhi::ResourceState::Undefined, rhi::ResourceState::Undefined);
    const GraphBuffer vertices = b.createBuffer({32, rhi::BufferUsage{}, rhi::MemoryKind::Upload, "vertices"});
    b.addPass(
        "upload", [vertices](PassBuilder &p) { p.write(vertices, BufferAccess::HostWrite); },
        [vertices](PassContext &c) {
            const auto bytes = c.device.map(c.buffer(vertices));
            std::memset(bytes.data(), 0x7f, bytes.size());
        });
    b.addPass(
        "draw", [out, vertices](PassBuilder &p) { p.colorTarget(out); p.read(vertices, BufferAccess::Vertex); },
        [](PassContext &c) { c.cmd.draw(3); });
    renderGraph.compile();

    const std::string dump = renderGraph.dump();
    CHECK(passBlock(dump, "upload").find("barrier") == npos);          // host writes need no barrier
    CHECK(passBlock(dump, "draw").find("-> VertexOrIndex") != npos);
    const rhi::BufferDesc &desc = device.describe(renderGraph.resolve(vertices));
    CHECK((desc.usage & rhi::BufferUsage::Vertex) != rhi::BufferUsage{});
    CHECK(desc.memory == rhi::MemoryKind::Upload);

    renderGraph.execute();
    CHECK(device.map(renderGraph.resolve(vertices))[0] == std::byte{0x7f});
}

void testValidation() {
    null::NullDevice device;
    graph::RenderGraph renderGraph(device);

    // One pass cannot read and write the same resource.
    auto &b = renderGraph.begin();
    const GraphTexture t = b.createTexture(texture("T"));
    b.addPass("seed", [t](PassBuilder &p) { p.colorTarget(t); }, [](PassContext &) {});
    b.addPass("both", [t](PassBuilder &p) { p.read(t, TextureAccess::ShaderRead); p.write(t, TextureAccess::ShaderWrite); p.sideEffect(); },
              [](PassContext &) {});
    CHECK_THROWS_WITH(renderGraph.compile(), GraphError, "both reads and writes");

    // Render targets of one pass share a size.
    auto &b2 = renderGraph.begin();
    const GraphTexture small = b2.createTexture(texture("small", 32));
    const GraphTexture large = b2.createTexture(texture("large", 64));
    b2.addPass("mismatch", [=](PassBuilder &p) { p.colorTarget(small); p.colorTarget(large); p.sideEffect(); }, [](PassContext &) {});
    CHECK_THROWS_WITH(renderGraph.compile(), GraphError, "different sizes");

    // read() and write() reject the wrong kind of access.
    auto &b3 = renderGraph.begin();
    const GraphTexture t3 = b3.createTexture(texture("T"));
    CHECK_THROWS_WITH(b3.addPass("bad", [t3](PassBuilder &p) { p.read(t3, TextureAccess::ShaderWrite); }, nullptr), GraphError, "read()");
    CHECK_THROWS_WITH(b3.addPass("bad", [t3](PassBuilder &p) { p.write(t3, TextureAccess::ShaderRead); }, nullptr), GraphError, "write()");
    // A pass whose setup throws is rolled back and leaves the graph usable.
    CHECK_THROWS_WITH(b3.addPass("bad", [](PassBuilder &p) { p.read(GraphTexture{}, TextureAccess::ShaderRead); }, nullptr),
                      GraphError, "invalid texture handle");
    CHECK(renderGraph.dump().find("pass ") == npos);

    // Two states of one resource in one pass are a conflict the barrier planner reports.
    auto &b4 = renderGraph.begin();
    const GraphTexture t4 = b4.createTexture(texture("T"));
    b4.addPass("seed", [t4](PassBuilder &p) { p.colorTarget(t4); }, [](PassContext &) {});
    b4.addPass("conflict",
               [t4](PassBuilder &p) {
                   p.read(t4, TextureAccess::ShaderRead);
                   p.read(t4, TextureAccess::TransferSrc);
                   p.sideEffect();
               },
               [](PassContext &) {});
    CHECK_THROWS_WITH(renderGraph.compile(), GraphError, "as both");

    // execute() needs a successful compile().
    CHECK_THROWS_WITH(renderGraph.execute(), std::logic_error, "compile");
}

} // namespace

int main() {
    testBarriersBetweenPasses();
    testCulling();
    testCullingFollowsDependencies();
    testReadOfUnwrittenTransient();
    testCapabilityGating();
    testTechniqueSupport();
    testPoolReuse();
    testImportedFinalState();
    testWriteAfterWrite();
    testReadAfterReadNeedsNoBarrier();
    testHostWrites();
    testValidation();
    std::cout << "graph test passed" << std::endl;
    return 0;
}
