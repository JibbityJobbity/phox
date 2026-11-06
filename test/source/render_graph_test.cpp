// Render graph logic, checked against a backend that records what it is asked to do. No GPU needed.
#include "phox/RenderGraph.hpp"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::cerr << "CHECK failed: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

using namespace phox;

namespace {

std::string name(ImageUsage u) {
    switch (u) {
    case ImageUsage::Undefined: return "Undefined";
    case ImageUsage::ColorAttachment: return "Color";
    case ImageUsage::DepthAttachment: return "Depth";
    case ImageUsage::DepthReadOnly: return "DepthRO";
    case ImageUsage::Sampled: return "Sampled";
    case ImageUsage::StorageRead: return "StorageRead";
    case ImageUsage::StorageWrite: return "StorageWrite";
    case ImageUsage::TransferSrc: return "TransferSrc";
    case ImageUsage::TransferDst: return "TransferDst";
    case ImageUsage::Present: return "Present";
    }
    return "?";
}

std::string name(BufferUsage u) {
    switch (u) {
    case BufferUsage::Undefined: return "Undefined";
    case BufferUsage::Vertex: return "Vertex";
    case BufferUsage::Index: return "Index";
    case BufferUsage::Uniform: return "Uniform";
    case BufferUsage::StorageRead: return "StorageRead";
    case BufferUsage::StorageWrite: return "StorageWrite";
    case BufferUsage::Indirect: return "Indirect";
    case BufferUsage::TransferSrc: return "TransferSrc";
    case BufferUsage::TransferDst: return "TransferDst";
    case BufferUsage::HostRead: return "HostRead";
    case BufferUsage::HostWrite: return "HostWrite";
    }
    return "?";
}

// Records what the graph asks of a backend, in order.
class RecordingBackend : public GraphBackend, public PassContext {
public:
    explicit RecordingBackend(const RenderGraph &graph) : m_graph(graph) {}
    std::vector<std::string> log;

    void acquire(const ImageInfo &image) override { log.push_back("acquire " + image.name); }
    void acquire(const BufferInfo &buffer) override { log.push_back("acquire " + buffer.name); }
    void release(const ImageInfo &image) override { log.push_back("release " + image.name); }
    void release(const BufferInfo &buffer) override { log.push_back("release " + buffer.name); }
    void barriers(const BarrierBatch &batch) override {
        for (const ImageBarrier &b : batch.images) {
            log.push_back("barrier " + m_graph.imageInfo(b.image).name + " " + name(b.from) + "->" + name(b.to));
        }
        for (const BufferBarrier &b : batch.buffers) {
            log.push_back("barrier " + m_graph.bufferInfo(b.buffer).name + " " + name(b.from) + "->" + name(b.to));
        }
    }
    void beginSubmission(const Submission &submission, std::uint32_t index) override {
        if (logSubmissions) {
            log.push_back("submit " + std::to_string(index) +
                          (submission.queue == Queue::AsyncCompute ? " async" : " graphics") +
                          (submission.wait ? " wait " + std::to_string(*submission.wait) : std::string()));
        }
    }
    bool logSubmissions = false;
    void beginPass(const CompiledPass &pass) override { log.push_back("begin " + pass.name); }
    void endPass(const CompiledPass &pass) override { log.push_back("end " + pass.name); }
    PassContext &passContext() override { return *this; }

private:
    const RenderGraph &m_graph;
};

const ImageDesc kColor{Extent2D{64, 64}, Format::RGBA8Unorm};
const ImageDesc kDepth{Extent2D{64, 64}, Format::D32Float};

template <class Exception = std::logic_error> bool throws(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const Exception &) {
        return true;
    }
    return false;
}

void headlessFrame() {
    // scene -> color -> readback into an imported host buffer, as the renderer does without a window.
    RenderGraph graph;
    RecordingBackend backend(graph);
    const ImageHandle color = graph.createImage("color", kColor);
    const BufferHandle staging = graph.importBuffer("staging", {64 * 64 * 4}, BufferUsage::Undefined, BufferUsage::HostRead);
    ImageHandle drawn;
    graph.addPass("scene", PassType::Graphics, [&](PassBuilder &b) { drawn = b.colorAttachment(color, LoadOp::Clear); },
                  nullptr);
    graph.addPass("readback", PassType::Transfer,
                  [&](PassBuilder &b) {
                      b.read(drawn, ImageUsage::TransferSrc);
                      b.write(staging, BufferUsage::TransferDst);
                  },
                  nullptr);
    graph.execute(backend);

    const std::vector<std::string> expected = {
        "acquire color",
        "barrier color Undefined->Color",
        "begin scene",
        "end scene",
        "barrier color Color->TransferSrc",
        "barrier staging Undefined->TransferDst",
        "begin readback",
        "end readback",
        "release color",
        "barrier staging TransferDst->HostRead",
    };
    CHECK(backend.log == expected);
    CHECK(graph.imageInfo(color).usage == (usageBit(ImageUsage::ColorAttachment) | usageBit(ImageUsage::TransferSrc)));
    CHECK(drawn.index == color.index && drawn.version == 1);
}

void presentFrame() {
    RenderGraph graph;
    RecordingBackend backend(graph);
    const ImageHandle color = graph.createImage("color", kColor);
    const ImageHandle swap = graph.importImage("backbuffer", kColor, ImageUsage::Undefined, ImageUsage::Present);
    ImageHandle drawn;
    graph.addPass("scene", PassType::Graphics, [&](PassBuilder &b) { drawn = b.colorAttachment(color, LoadOp::Clear); },
                  nullptr);
    graph.addPass("blit", PassType::Transfer,
                  [&](PassBuilder &b) {
                      b.read(drawn, ImageUsage::TransferSrc);
                      b.write(swap, ImageUsage::TransferDst);
                  },
                  nullptr);
    graph.execute(backend);

    // The import is never acquired or released, ends in Present, and is first used as a transfer target.
    CHECK(backend.log.back() == "barrier backbuffer TransferDst->Present");
    for (const std::string &line : backend.log) {
        CHECK(line != "acquire backbuffer" && line != "release backbuffer");
    }
    const auto first = graph.firstUse(swap);
    CHECK(first && first->usage == ImageUsage::TransferDst && first->pass == PassType::Transfer);
}

void untouchedImportStillTransitions() {
    RenderGraph graph;
    const ImageHandle swap = graph.importImage("backbuffer", kColor, ImageUsage::Undefined, ImageUsage::Present);
    graph.compile();
    CHECK(!graph.firstUse(swap).has_value());
    CHECK(graph.finalBarriers().images.size() == 1);
    CHECK(graph.finalBarriers().images[0].from == ImageUsage::Undefined);
    CHECK(graph.finalBarriers().images[0].to == ImageUsage::Present);
}

void cullsUnusedPasses() {
    RenderGraph graph;
    RecordingBackend backend(graph);
    const ImageHandle used = graph.createImage("used", kColor);
    const ImageHandle unused = graph.createImage("unused", kColor);
    graph.addPass("dead", PassType::Graphics, [&](PassBuilder &b) { b.colorAttachment(unused, LoadOp::Clear); }, nullptr);
    ImageHandle drawn;
    graph.addPass("scene", PassType::Graphics, [&](PassBuilder &b) { drawn = b.colorAttachment(used, LoadOp::Clear); },
                  nullptr);
    graph.addPass("readback", PassType::Transfer,
                  [&](PassBuilder &b) {
                      b.read(drawn, ImageUsage::TransferSrc);
                      b.keep();
                  },
                  nullptr);
    graph.execute(backend);

    CHECK(graph.compiledPasses()[0].culled);
    CHECK(!graph.compiledPasses()[1].culled);
    CHECK(!graph.compiledPasses()[2].culled);
    for (const std::string &line : backend.log) {
        CHECK(line.find("unused") == std::string::npos);
        CHECK(line != "begin dead");
    }
}

void culledProducerChain() {
    // A producer is live only if a live pass consumes what it made: a -> b -> (nothing) culls both.
    RenderGraph graph;
    const ImageHandle a = graph.createImage("a", kColor);
    const ImageHandle b = graph.createImage("b", kColor);
    ImageHandle aDrawn;
    graph.addPass("pa", PassType::Graphics, [&](PassBuilder &p) { aDrawn = p.colorAttachment(a, LoadOp::Clear); }, nullptr);
    graph.addPass("pb", PassType::Transfer,
                  [&](PassBuilder &p) {
                      p.read(aDrawn, ImageUsage::TransferSrc);
                      p.write(b, ImageUsage::TransferDst);
                  },
                  nullptr);
    graph.compile();
    CHECK(graph.compiledPasses()[0].culled);
    CHECK(graph.compiledPasses()[1].culled);
}

void versionsMakeCullingExact() {
    // "first" clears the image, "second" clears it again: nothing ever sees version 1, so "first" goes,
    // even though a live pass reads the image.
    RenderGraph graph;
    const ImageHandle image = graph.createImage("image", kColor);
    ImageHandle v1, v2;
    graph.addPass("first", PassType::Graphics, [&](PassBuilder &b) { v1 = b.colorAttachment(image, LoadOp::Clear); }, nullptr);
    graph.addPass("second", PassType::Graphics, [&](PassBuilder &b) { v2 = b.colorAttachment(v1, LoadOp::Clear); }, nullptr);
    graph.addPass("use", PassType::Transfer,
                  [&](PassBuilder &b) {
                      b.read(v2, ImageUsage::TransferSrc);
                      b.keep();
                  },
                  nullptr);
    graph.compile();
    CHECK(graph.compiledPasses()[0].culled);
    CHECK(!graph.compiledPasses()[1].culled);
    CHECK(v1.version == 1 && v2.version == 2);

    // With LoadOp::Load the second pass builds on the first, which keeps it alive.
    RenderGraph loading;
    const ImageHandle img = loading.createImage("image", kColor);
    ImageHandle l1, l2;
    loading.addPass("first", PassType::Graphics, [&](PassBuilder &b) { l1 = b.colorAttachment(img, LoadOp::Clear); }, nullptr);
    loading.addPass("second", PassType::Graphics, [&](PassBuilder &b) { l2 = b.colorAttachment(l1, LoadOp::Load); }, nullptr);
    loading.addPass("use", PassType::Transfer,
                    [&](PassBuilder &b) {
                        b.read(l2, ImageUsage::TransferSrc);
                        b.keep();
                    },
                    nullptr);
    loading.compile();
    CHECK(!loading.compiledPasses()[0].culled);
}

void rejectsInvalidDeclarations() {
    RenderGraph graph;
    const ImageHandle color = graph.createImage("color", kColor);
    const ImageHandle depth = graph.createImage("depth", kDepth);

    // Reading, or loading, an image nothing has written.
    CHECK(throws([&] { graph.addPass("r", PassType::Transfer, [&](PassBuilder &b) { b.read(color, ImageUsage::TransferSrc); }, nullptr); }));
    CHECK(throws([&] { graph.addPass("l", PassType::Graphics, [&](PassBuilder &b) { b.colorAttachment(color, LoadOp::Load); }, nullptr); }));
    // An unknown resource.
    CHECK(throws<std::out_of_range>([&] {
        graph.addPass("u", PassType::Graphics, [&](PassBuilder &b) { b.colorAttachment(ImageHandle{99, 0}, LoadOp::Clear); }, nullptr);
    }));
    // Attachments in a compute pass; a graphics pass with none.
    CHECK(throws([&] { graph.addPass("c", PassType::Compute, [&](PassBuilder &b) { b.colorAttachment(color, LoadOp::Clear); }, nullptr); }));
    CHECK(throws([&] { graph.addPass("g", PassType::Graphics, [&](PassBuilder &) {}, nullptr); }));
    // Formats that don't fit the attachment kind.
    CHECK(throws([&] { graph.addPass("d", PassType::Graphics, [&](PassBuilder &b) { b.colorAttachment(depth, LoadOp::Clear); }, nullptr); }));
    CHECK(throws([&] { graph.addPass("e", PassType::Graphics, [&](PassBuilder &b) { b.depthAttachment(color, LoadOp::Clear); }, nullptr); }));
    // Usages that are not reads/writes, or attachment usages through write().
    CHECK(throws([&] { graph.addPass("w", PassType::Transfer, [&](PassBuilder &b) { b.write(color, ImageUsage::TransferSrc); }, nullptr); }));
    CHECK(throws([&] { graph.addPass("x", PassType::Graphics, [&](PassBuilder &b) { b.write(color, ImageUsage::ColorAttachment); }, nullptr); }));

    // None of that changed the graph: the image is still at version 0 and can be used.
    ImageHandle drawn;
    graph.addPass("ok", PassType::Graphics, [&](PassBuilder &b) { drawn = b.colorAttachment(color, LoadOp::Clear); }, nullptr);
    CHECK(drawn.version == 1);

    // A stale version (written since), and one image used two ways in one pass.
    CHECK(throws([&] { graph.addPass("s", PassType::Graphics, [&](PassBuilder &b) { b.colorAttachment(color, LoadOp::Clear); }, nullptr); }));
    CHECK(throws([&] {
        graph.addPass("two", PassType::Graphics,
                      [&](PassBuilder &b) {
                          b.read(drawn, ImageUsage::Sampled);
                          b.colorAttachment(drawn, LoadOp::Load);
                      },
                      nullptr);
    }));
}

void writeAfterWriteGetsABarrier() {
    RenderGraph graph;
    const ImageHandle color = graph.createImage("color", kColor);
    ImageHandle v1, v2;
    graph.addPass("first", PassType::Graphics, [&](PassBuilder &b) { v1 = b.colorAttachment(color, LoadOp::Clear); }, nullptr);
    graph.addPass("second", PassType::Graphics, [&](PassBuilder &b) { v2 = b.colorAttachment(v1, LoadOp::Load); }, nullptr);
    graph.addPass("use", PassType::Transfer,
                  [&](PassBuilder &b) {
                      b.read(v2, ImageUsage::TransferSrc);
                      b.keep();
                  },
                  nullptr);
    graph.compile();
    const auto &barriers = graph.compiledPasses()[1].barriers.images;
    CHECK(barriers.size() == 1);
    CHECK(barriers[0].from == ImageUsage::ColorAttachment && barriers[0].to == ImageUsage::ColorAttachment);
}

void readAfterReadHasNoBarrier() {
    RenderGraph graph;
    const ImageHandle color = graph.createImage("color", kColor);
    ImageHandle drawn;
    graph.addPass("scene", PassType::Graphics, [&](PassBuilder &b) { drawn = b.colorAttachment(color, LoadOp::Clear); }, nullptr);
    for (const char *reader : {"r1", "r2"}) {
        graph.addPass(reader, PassType::Graphics,
                      [&](PassBuilder &b) {
                          b.read(drawn, ImageUsage::Sampled);
                          b.colorAttachment(graph.createImage(std::string(reader) + " out", kColor), LoadOp::Clear);
                          b.keep();
                      },
                      nullptr);
    }
    graph.compile();
    const auto sampledBarriers = [&](std::size_t pass) {
        std::size_t n = 0;
        for (const ImageBarrier &b : graph.compiledPasses()[pass].barriers.images) {
            if (b.image == drawn.index) ++n;
        }
        return n;
    };
    CHECK(sampledBarriers(1) == 1);
    CHECK(sampledBarriers(2) == 0);
}

void bufferHazardsAcrossPassTypes() {
    // A compute pass fills a buffer that a graphics pass then draws from.
    RenderGraph graph;
    const BufferHandle vertices = graph.createBuffer("vertices", {1024});
    const ImageHandle color = graph.createImage("color", kColor);
    BufferHandle filled;
    graph.addPass("generate", PassType::Compute,
                  [&](PassBuilder &b) { filled = b.write(vertices, BufferUsage::StorageWrite); }, nullptr);
    graph.addPass("draw", PassType::Graphics,
                  [&](PassBuilder &b) {
                      b.read(filled, BufferUsage::Vertex);
                      b.colorAttachment(color, LoadOp::Clear);
                      b.keep();
                  },
                  nullptr);
    graph.compile();
    const auto &generate = graph.compiledPasses()[0];
    const auto &draw = graph.compiledPasses()[1];
    CHECK(generate.acquireBuffers == std::vector<std::uint32_t>{vertices.index});
    CHECK(draw.releaseBuffers == std::vector<std::uint32_t>{vertices.index});
    CHECK(draw.barriers.buffers.size() == 1);
    const BufferBarrier &b = draw.barriers.buffers[0];
    CHECK(b.from == BufferUsage::StorageWrite && b.to == BufferUsage::Vertex);
    CHECK(b.fromPass == PassType::Compute && b.toPass == PassType::Graphics);
    CHECK(graph.bufferInfo(vertices).usage == (usageBit(BufferUsage::StorageWrite) | usageBit(BufferUsage::Vertex)));
}

void depthReadOnlyAfterDepthWrite() {
    RenderGraph graph;
    const ImageHandle depth = graph.createImage("depth", kDepth);
    const ImageHandle color = graph.createImage("color", kColor);
    ImageHandle written, lit;
    graph.addPass("prepass", PassType::Graphics, [&](PassBuilder &b) { written = b.depthAttachment(depth, LoadOp::Clear); }, nullptr);
    graph.addPass("shade", PassType::Graphics,
                  [&](PassBuilder &b) {
                      b.depthReadOnly(written);
                      lit = b.colorAttachment(color, LoadOp::Clear);
                      b.keep();
                  },
                  nullptr);
    graph.compile();
    const CompiledPass &shade = graph.compiledPasses()[1];
    std::size_t depthBarriers = 0;
    for (const ImageBarrier &b : shade.barriers.images) {
        if (b.image == depth.index) {
            ++depthBarriers;
            CHECK(b.from == ImageUsage::DepthAttachment && b.to == ImageUsage::DepthReadOnly);
        }
    }
    CHECK(depthBarriers == 1);
    CHECK(shade.depthAttachment && shade.depthAttachment->readOnly);
    CHECK(!graph.compiledPasses()[0].culled); // the read-only use keeps the prepass alive
}

void relativeExtentsResolve() {
    RenderGraph graph(Extent2D{200, 100});
    const ImageHandle full = graph.createImage("full", ImageDesc{RelativeExtent{1.0f}, Format::RGBA8Unorm});
    const ImageHandle half = graph.createImage("half", ImageDesc{RelativeExtent{0.5f}, Format::RGBA8Unorm});
    graph.addPass("a", PassType::Graphics,
                  [&](PassBuilder &b) {
                      b.colorAttachment(full, LoadOp::Clear);
                      b.keep();
                  },
                  nullptr);
    graph.addPass("b", PassType::Graphics,
                  [&](PassBuilder &b) {
                      b.colorAttachment(half, LoadOp::Clear);
                      b.keep();
                  },
                  nullptr);
    graph.compile();
    CHECK(graph.imageInfo(full).extent == (Extent2D{200, 100}));
    CHECK(graph.imageInfo(half).extent == (Extent2D{100, 50}));
    CHECK(graph.compiledPasses()[1].renderArea == (Extent2D{100, 50}));

    // Attachments of different sizes in one pass, and relative sizes without an output extent.
    RenderGraph mixed(Extent2D{200, 100});
    const ImageHandle m1 = mixed.createImage("m1", ImageDesc{RelativeExtent{1.0f}, Format::RGBA8Unorm});
    const ImageHandle m2 = mixed.createImage("m2", ImageDesc{RelativeExtent{0.5f}, Format::RGBA8Unorm});
    mixed.addPass("mixed", PassType::Graphics,
                  [&](PassBuilder &b) {
                      b.colorAttachment(m1, LoadOp::Clear);
                      b.colorAttachment(m2, LoadOp::Clear);
                      b.keep();
                  },
                  nullptr);
    CHECK(throws([&] { mixed.compile(); }));
    RenderGraph noOutput;
    noOutput.createImage("relative", ImageDesc{RelativeExtent{1.0f}, Format::RGBA8Unorm});
    CHECK(throws([&] { noOutput.compile(); }));
}

void passDataFlowsFromSetupToExecute() {
    struct SceneData {
        ImageHandle color;
        int marker = 0;
    };
    RenderGraph graph;
    RecordingBackend backend(graph);
    const SceneData &scene = graph.addPass<SceneData>(
        "scene", PassType::Graphics,
        [&](PassBuilder &b, SceneData &data) {
            data.color = b.colorAttachment(graph.createImage("color", kColor), LoadOp::Clear);
            data.marker = 42;
        },
        [&](const SceneData &data, PassContext &) { backend.log.push_back("scene data " + std::to_string(data.marker)); });
    graph.addPass("use", PassType::Transfer,
                  [&](PassBuilder &b) {
                      b.read(scene.color, ImageUsage::TransferSrc); // the handle setup produced
                      b.keep();
                  },
                  nullptr);
    graph.execute(backend);
    CHECK(scene.color.version == 1);
    bool ran = false;
    for (const std::string &line : backend.log) ran = ran || line == "scene data 42";
    CHECK(ran);
}

void dotShowsPassesAndCulling() {
    RenderGraph graph;
    const ImageHandle color = graph.createImage("color", kColor);
    const ImageHandle unused = graph.createImage("unused", kColor);
    ImageHandle drawn;
    graph.addPass("dead", PassType::Graphics, [&](PassBuilder &b) { b.colorAttachment(unused, LoadOp::Clear); }, nullptr);
    graph.addPass("scene", PassType::Graphics, [&](PassBuilder &b) { drawn = b.colorAttachment(color, LoadOp::Clear); }, nullptr);
    graph.addPass("readback", PassType::Transfer,
                  [&](PassBuilder &b) {
                      b.read(drawn, ImageUsage::TransferSrc);
                      b.keep();
                  },
                  nullptr);
    CHECK(throws([&] { (void)graph.toDot(); })); // not compiled yet
    graph.compile();
    const std::string dot = graph.toDot();
    CHECK(dot.rfind("digraph", 0) == 0);
    for (const char *needle : {"scene", "readback", "dead\\ngraphics (culled)", "color v1", "TransferSrc"}) {
        if (dot.find(needle) == std::string::npos) {
            std::cerr << "missing '" << needle << "' in:\n" << dot;
            std::exit(1);
        }
    }
}


// A fluid step on the async queue, whose output the graphics queue draws: the draw waits for it, the pass
// before the draw does not.
void asyncComputeSplitsSubmissions() {
    RenderGraph graph;
    graph.setAsyncCompute(true);
    RecordingBackend backend(graph);
    backend.logSubmissions = true;
    const BufferHandle particles = graph.createBuffer("particles", {1024});
    const BufferHandle vertices = graph.createBuffer("vertices", {4096});
    const ImageHandle color = graph.createImage("color", kColor);
    const ImageHandle swap = graph.importImage("backbuffer", kColor, ImageUsage::Undefined, ImageUsage::Present);
    BufferHandle simulated, surface;
    ImageHandle sky, drawn;
    graph.addPass("simulate", PassType::Compute,
                  [&](PassBuilder &b) {
                      simulated = b.write(particles, BufferUsage::StorageWrite);
                      b.async();
                  },
                  nullptr);
    graph.addPass("surface", PassType::Compute,
                  [&](PassBuilder &b) {
                      b.read(simulated, BufferUsage::StorageRead);
                      surface = b.write(vertices, BufferUsage::StorageWrite);
                      b.async();
                  },
                  nullptr);
    graph.addPass("sky", PassType::Graphics, [&](PassBuilder &b) { sky = b.colorAttachment(color, LoadOp::Clear); }, nullptr);
    graph.addPass("draw", PassType::Graphics,
                  [&](PassBuilder &b) {
                      b.read(surface, BufferUsage::Vertex);
                      drawn = b.colorAttachment(sky, LoadOp::Load);
                  },
                  nullptr);
    graph.addPass("blit", PassType::Transfer,
                  [&](PassBuilder &b) {
                      b.read(drawn, ImageUsage::TransferSrc);
                      b.write(swap, ImageUsage::TransferDst);
                  },
                  nullptr);
    graph.execute(backend);

    // 0: the work before the graph only; 1: the async passes; 2: sky; 3: what needs the async results.
    const std::vector<Submission> &subs = graph.submissions();
    CHECK(subs.size() == 4);
    CHECK(subs[0].queue == Queue::Graphics && subs[0].passes.empty() && !subs[0].signals);
    CHECK(subs[1].queue == Queue::AsyncCompute && (subs[1].passes == std::vector<std::uint32_t>{0, 1}) && subs[1].signals);
    CHECK(subs[2].queue == Queue::Graphics && subs[2].passes == std::vector<std::uint32_t>{2} && !subs[2].wait);
    CHECK(subs[3].queue == Queue::Graphics && (subs[3].passes == std::vector<std::uint32_t>{3, 4}) && subs[3].wait == 1u);
    CHECK(subs[3].finalBarriers.images.size() == 1 && subs[2].finalBarriers.empty());
    CHECK(graph.compiledPasses()[0].queue == Queue::AsyncCompute && graph.compiledPasses()[3].queue == Queue::Graphics);

    const std::vector<std::string> expected = {
        "submit 0 graphics",
        "submit 1 async",
        "acquire particles",
        "barrier particles Undefined->StorageWrite",
        "begin simulate",
        "end simulate",
        "acquire vertices",
        "barrier particles StorageWrite->StorageRead",
        "barrier vertices Undefined->StorageWrite",
        "begin surface",
        "end surface",
        "release particles",
        "submit 2 graphics",
        "acquire color",
        "barrier color Undefined->Color",
        "begin sky",
        "end sky",
        "submit 3 graphics wait 1",
        "barrier color Color->Color",
        "barrier vertices StorageWrite->Vertex",
        "begin draw",
        "end draw",
        "release vertices",
        "barrier color Color->TransferSrc",
        "barrier backbuffer Undefined->TransferDst",
        "begin blit",
        "end blit",
        "release color",
        "barrier backbuffer TransferDst->Present",
    };
    if (backend.log != expected) {
        for (const std::string &line : backend.log) std::cerr << line << "\n";
    }
    CHECK(backend.log == expected);
    // Only the draw's use of the vertices crosses queues.
    for (const BufferBarrier &b : graph.compiledPasses()[3].barriers.buffers) CHECK(b.crossQueue);
    for (const ImageBarrier &b : graph.compiledPasses()[3].barriers.images) CHECK(!b.crossQueue);
    const QueueMask both = queueBit(Queue::Graphics) | queueBit(Queue::AsyncCompute);
    CHECK(graph.bufferInfo(vertices).queues == both);
    CHECK(graph.bufferInfo(particles).queues == queueBit(Queue::AsyncCompute));
    CHECK(graph.imageInfo(color).queues == queueBit(Queue::Graphics));
    const auto first = graph.firstUse(swap);
    CHECK(first && first->submission == 3);
}

// The frame pipelining a simulation wants: the async queue computes step n + 1 into one buffer while the
// graphics queue draws step n from the other, written on the async queue last frame. The draw waits only
// for the async work submitted before this graph, so the two queues overlap; the async queue's next use
// of the drawn buffer waits for the draw.
void pipelinedImportsOverlap() {
    RenderGraph graph;
    graph.setAsyncCompute(true);
    const BufferHandle next = graph.importBuffer("surface next", {64}, BufferUsage::StorageWrite,
                                                 BufferUsage::StorageWrite, Queue::AsyncCompute);
    const BufferHandle shown = graph.importBuffer("surface shown", {64}, BufferUsage::StorageWrite,
                                                  BufferUsage::StorageWrite, Queue::AsyncCompute);
    const ImageHandle color = graph.createImage("color", kColor);
    graph.addPass("simulate", PassType::Compute,
                  [&](PassBuilder &b) {
                      b.write(next, BufferUsage::StorageWrite);
                      b.async();
                  },
                  nullptr);
    graph.addPass("draw", PassType::Graphics,
                  [&](PassBuilder &b) {
                      b.read(shown, BufferUsage::Vertex);
                      b.colorAttachment(color, LoadOp::Clear);
                      b.keep();
                  },
                  nullptr);
    graph.compile();
    const std::vector<Submission> &subs = graph.submissions();
    CHECK(subs.size() == 4);
    CHECK(subs[1].queue == Queue::AsyncCompute && subs[1].passes == std::vector<std::uint32_t>{0});
    CHECK(!subs[1].signals); // nothing this frame waits for the simulation
    CHECK(subs[2].queue == Queue::Graphics && subs[2].passes == std::vector<std::uint32_t>{1});
    CHECK(!subs[2].wait && subs[2].waitEarlier && subs[2].signals);
    // The shown buffer goes home: a final async submission waits for the draw.
    CHECK(subs[3].queue == Queue::AsyncCompute && subs[3].wait == 2u && subs[3].passes.empty());
    CHECK(subs[3].finalBarriers.buffers.size() == 1 && subs[3].finalBarriers.buffers[0].buffer == shown.index);
    CHECK(graph.compiledPasses()[1].barriers.buffers[0].crossQueue);
}

// Without an async queue the same passes run in order on the graphics queue, as one submission.
void asyncPassesWithoutAsyncCompute() {
    RenderGraph graph;
    RecordingBackend backend(graph);
    const BufferHandle data = graph.createBuffer("data", {256});
    const ImageHandle color = graph.createImage("color", kColor);
    BufferHandle written;
    graph.addPass("compute", PassType::Compute,
                  [&](PassBuilder &b) {
                      written = b.write(data, BufferUsage::StorageWrite);
                      b.async();
                  },
                  nullptr);
    graph.addPass("draw", PassType::Graphics,
                  [&](PassBuilder &b) {
                      b.read(written, BufferUsage::Vertex);
                      b.colorAttachment(color, LoadOp::Clear);
                      b.keep();
                  },
                  nullptr);
    graph.execute(backend);
    CHECK(graph.submissions().size() == 1);
    CHECK((graph.submissions()[0].passes == std::vector<std::uint32_t>{0, 1}));
    for (const CompiledPass &pass : graph.compiledPasses()) {
        CHECK(pass.queue == Queue::Graphics && pass.submission == 0);
        for (const BufferBarrier &b : pass.barriers.buffers) CHECK(!b.crossQueue);
    }
    CHECK(graph.bufferInfo(data).queues == queueBit(Queue::Graphics));
    CHECK(throws([&] { graph.addPass("not compute", PassType::Graphics, [&](PassBuilder &b) { b.async(); }, nullptr); }));
}

// An import arrives and leaves on its home queue: a pass on the other queue waits for the home queue's
// first submission, and a final submission on the home queue waits for the last use.
void importsReturnToTheirHomeQueue() {
    RenderGraph graph;
    graph.setAsyncCompute(true);
    const BufferHandle state = graph.importBuffer("state", {64}, BufferUsage::StorageWrite, BufferUsage::StorageWrite);
    const BufferHandle field = graph.importBuffer("field", {64}, BufferUsage::StorageWrite, BufferUsage::StorageWrite,
                                                  Queue::AsyncCompute);
    graph.addPass("step", PassType::Compute,
                  [&](PassBuilder &b) {
                      b.write(state, BufferUsage::StorageWrite);
                      b.read(field, BufferUsage::StorageRead);
                      b.async();
                  },
                  nullptr);
    graph.compile();
    const std::vector<Submission> &subs = graph.submissions();
    CHECK(subs.size() == 3);
    CHECK(subs[0].queue == Queue::Graphics && subs[0].passes.empty() && subs[0].signals);
    CHECK(subs[1].queue == Queue::AsyncCompute && subs[1].wait == 0u && subs[1].signals);
    CHECK(subs[2].queue == Queue::Graphics && subs[2].wait == 1u && subs[2].passes.empty());
    // state goes back to the graphics queue; field never left the async one, where it returns to StorageWrite.
    CHECK(subs[2].finalBarriers.buffers.size() == 1 && subs[2].finalBarriers.buffers[0].crossQueue);
    CHECK(subs[1].finalBarriers.buffers.size() == 1 && !subs[1].finalBarriers.buffers[0].crossQueue);
    CHECK(subs[1].finalBarriers.buffers[0].buffer == field.index);
    const BufferBarrier &step = graph.compiledPasses()[0].barriers.buffers[0];
    CHECK(step.buffer == state.index && step.crossQueue);
}

// Every use of a resource after a use on the other queue is in a submission that waits for that one (or
// a later one on the same queue), and waits only ever point backwards. Checked on many random graphs.
void randomGraphsAreOrdered() {
    std::uint32_t seed = 12345;
    const auto next = [&](std::uint32_t n) {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) % n;
    };
    for (int round = 0; round < 500; ++round) {
        RenderGraph graph;
        graph.setAsyncCompute(true);
        constexpr int kBuffers = 4;
        std::vector<BufferHandle> buffers;
        std::vector<std::optional<Queue>> homes(kBuffers);
        for (int i = 0; i < kBuffers; ++i) {
            if (next(3) == 0) {
                homes[i] = next(2) == 0 ? Queue::Graphics : Queue::AsyncCompute;
                buffers.push_back(graph.importBuffer("b" + std::to_string(i), {16}, BufferUsage::StorageWrite,
                                                     BufferUsage::StorageWrite, *homes[i]));
            } else {
                buffers.push_back(graph.createBuffer("b" + std::to_string(i), {16}));
            }
        }
        std::vector<bool> written(kBuffers, false);
        for (int i = 0; i < kBuffers; ++i) written[i] = graph.bufferInfo(buffers[i]).imported;
        // uses[b]: (pass, writes) in order
        std::vector<std::vector<int>> uses(kBuffers);
        const int passCount = 3 + static_cast<int>(next(8));
        for (int p = 0; p < passCount; ++p) {
            const bool async = next(2) == 0;
            graph.addPass("p" + std::to_string(p), PassType::Compute,
                          [&](PassBuilder &b) {
                              for (int i = 0; i < kBuffers; ++i) {
                                  const std::uint32_t action = next(3); // 0: none, 1: read, 2: write
                                  if (action == 1 && written[i]) {
                                      b.read(buffers[i], BufferUsage::StorageRead);
                                      uses[i].push_back(p);
                                  } else if (action == 2) {
                                      buffers[i] = b.write(buffers[i], BufferUsage::StorageWrite);
                                      written[i] = true;
                                      uses[i].push_back(p);
                                  }
                              }
                              if (async) b.async();
                              b.keep();
                          },
                          nullptr);
        }
        graph.compile();
        const std::vector<Submission> &subs = graph.submissions();
        const std::vector<CompiledPass> &passes = graph.compiledPasses();
        for (std::size_t s = 0; s < subs.size(); ++s) {
            if (subs[s].wait) {
                CHECK(*subs[s].wait < s && subs[*subs[s].wait].queue != subs[s].queue && subs[*subs[s].wait].signals);
            }
            for (std::uint32_t p : subs[s].passes) CHECK(passes[p].submission == s && passes[p].queue == subs[s].queue);
        }
        // Does submission @p later start after submission @p earlier has completed?
        const auto after = [&](std::uint32_t earlier, std::uint32_t later) {
            if (subs[earlier].queue == subs[later].queue) return earlier <= later;
            const auto &wait = subs[later].wait;
            return wait && *wait >= earlier && subs[*wait].queue == subs[earlier].queue;
        };
        for (int i = 0; i < kBuffers; ++i) {
            // An import first used on the other queue waits for what came before it on its home queue.
            if (homes[i] && !uses[i].empty() && passes[uses[i][0]].queue != *homes[i]) {
                const Submission &first = subs[passes[uses[i][0]].submission];
                CHECK(first.wait || (*homes[i] == Queue::AsyncCompute && first.waitEarlier));
            }
            for (std::size_t u = 1; u < uses[i].size(); ++u) {
                CHECK(after(passes[uses[i][u - 1]].submission, passes[uses[i][u]].submission));
            }
        }
    }
}

} // namespace

int main() {
    headlessFrame();
    presentFrame();
    untouchedImportStillTransitions();
    cullsUnusedPasses();
    culledProducerChain();
    versionsMakeCullingExact();
    rejectsInvalidDeclarations();
    writeAfterWriteGetsABarrier();
    readAfterReadHasNoBarrier();
    bufferHazardsAcrossPassTypes();
    depthReadOnlyAfterDepthWrite();
    relativeExtentsResolve();
    passDataFlowsFromSetupToExecute();
    dotShowsPassesAndCulling();
    asyncComputeSplitsSubmissions();
    asyncPassesWithoutAsyncCompute();
    importsReturnToTheirHomeQueue();
    pipelinedImportsOverlap();
    randomGraphsAreOrdered();
    std::cout << "render graph tests passed" << std::endl;
    return 0;
}
