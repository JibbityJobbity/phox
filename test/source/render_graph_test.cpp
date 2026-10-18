#include "phox/RenderGraph.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::cerr << "CHECK failed: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1);                                                             \
        }                                                                             \
    } while (0)

using namespace phox;

namespace {

const char *name(ImageUsage u) {
    switch (u) {
    case ImageUsage::Undefined: return "Undefined";
    case ImageUsage::ColorAttachment: return "Color";
    case ImageUsage::TransferSrc: return "TransferSrc";
    case ImageUsage::TransferDst: return "TransferDst";
    case ImageUsage::Present: return "Present";
    }
    return "?";
}

// Records what the graph asks of a backend, in order.
class RecordingBackend : public GraphBackend, public PassContext {
public:
    std::vector<std::string> log;
    void allocate(const ImageInfo &image) override {
        log.push_back("alloc " + image.name + " mask=" + std::to_string(image.usageMask));
    }
    void release(const ImageInfo &image) override { log.push_back("release " + image.name); }
    void transition(const ImageInfo &image, ImageUsage from, ImageUsage to) override {
        log.push_back(std::string("transition ") + image.name + " " + name(from) + "->" + name(to));
    }
    PassContext &passContext() override { return *this; }
};

constexpr ImageDesc kDesc{64, 64, ImageFormat::RGBA8};

std::function<void(PassContext &)> mark(RecordingBackend &backend, std::string passName) {
    return [&backend, passName](PassContext &) { backend.log.push_back("pass " + passName); };
}

bool throwsLogicError(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const std::logic_error &) {
        return true;
    }
    return false;
}

void headlessGraph() {
    RecordingBackend backend;
    RenderGraph graph;
    const auto color = graph.createImage("color", kDesc);
    graph.addPass("scene", [&](PassBuilder &b) { b.write(color, ImageUsage::ColorAttachment); }, mark(backend, "scene"));
    graph.addPass("readback", [&](PassBuilder &b) { b.read(color, ImageUsage::TransferSrc); b.sideEffects(); },
                  mark(backend, "readback"));
    graph.execute(backend);

    const std::uint32_t mask = usageBit(ImageUsage::ColorAttachment) | usageBit(ImageUsage::TransferSrc);
    const std::vector<std::string> expected = {
        "alloc color mask=" + std::to_string(mask),
        "transition color Undefined->Color",
        "pass scene",
        "transition color Color->TransferSrc",
        "pass readback",
        "release color",
    };
    CHECK(backend.log == expected);
}

void presentGraph() {
    RecordingBackend backend;
    RenderGraph graph;
    const auto color = graph.createImage("color", kDesc);
    const auto swap = graph.importImage("backbuffer", kDesc, ImageUsage::Undefined, ImageUsage::Present);
    graph.addPass("scene", [&](PassBuilder &b) { b.write(color, ImageUsage::ColorAttachment); }, mark(backend, "scene"));
    graph.addPass("blit", [&](PassBuilder &b) { b.read(color, ImageUsage::TransferSrc); b.write(swap, ImageUsage::TransferDst); },
                  mark(backend, "blit"));
    graph.execute(backend);

    // The imported image is never allocated/released by the graph, and ends in Present.
    const std::vector<std::string> expected = {
        "alloc color mask=" + std::to_string(usageBit(ImageUsage::ColorAttachment) | usageBit(ImageUsage::TransferSrc)),
        "transition color Undefined->Color",
        "pass scene",
        "transition color Color->TransferSrc",
        "transition backbuffer Undefined->TransferDst",
        "pass blit",
        "release color",
        "transition backbuffer TransferDst->Present",
    };
    CHECK(backend.log == expected);
}

void cullsUnusedPasses() {
    RecordingBackend backend;
    RenderGraph graph;
    const auto used = graph.createImage("used", kDesc);
    const auto unused = graph.createImage("unused", kDesc);
    graph.addPass("dead", [&](PassBuilder &b) { b.write(unused, ImageUsage::ColorAttachment); }, mark(backend, "dead"));
    graph.addPass("scene", [&](PassBuilder &b) { b.write(used, ImageUsage::ColorAttachment); }, mark(backend, "scene"));
    graph.addPass("readback", [&](PassBuilder &b) { b.read(used, ImageUsage::TransferSrc); b.sideEffects(); },
                  mark(backend, "readback"));
    graph.compile();

    CHECK(graph.compiledPasses()[0].culled);
    CHECK(!graph.compiledPasses()[1].culled);
    graph.execute(backend);
    for (const auto &line : backend.log) {
        CHECK(line.find("unused") == std::string::npos);
        CHECK(line != "pass dead");
    }
}

void culledProducerChain() {
    // A producer is kept only if a live pass consumes it: a -> b -> (nothing live) culls both.
    RecordingBackend backend;
    RenderGraph graph;
    const auto a = graph.createImage("a", kDesc);
    const auto b = graph.createImage("b", kDesc);
    graph.addPass("pa", [&](PassBuilder &p) { p.write(a, ImageUsage::ColorAttachment); }, nullptr);
    graph.addPass("pb", [&](PassBuilder &p) { p.read(a, ImageUsage::TransferSrc); p.write(b, ImageUsage::TransferDst); }, nullptr);
    graph.compile();
    CHECK(graph.compiledPasses()[0].culled);
    CHECK(graph.compiledPasses()[1].culled);
}

void rejectsReadBeforeWrite() {
    RenderGraph graph;
    const auto color = graph.createImage("color", kDesc);
    graph.addPass("readback", [&](PassBuilder &b) { b.read(color, ImageUsage::TransferSrc); b.sideEffects(); }, nullptr);
    CHECK(throwsLogicError([&] { graph.compile(); }));
}

void rejectsConflictingUsagesInOnePass() {
    RenderGraph graph;
    const auto color = graph.createImage("color", kDesc);
    graph.addPass("bad", [&](PassBuilder &b) {
        b.write(color, ImageUsage::ColorAttachment);
        b.read(color, ImageUsage::TransferSrc);
        b.sideEffects();
    }, nullptr);
    CHECK(throwsLogicError([&] { graph.compile(); }));
}

void rejectsUnknownResource() {
    RenderGraph graph;
    bool threw = false;
    try {
        graph.addPass("bad", [&](PassBuilder &b) { b.write(ResourceHandle{99}, ImageUsage::ColorAttachment); }, nullptr);
    } catch (const std::out_of_range &) {
        threw = true;
    }
    CHECK(threw);
}

void writeAfterWriteGetsABarrier() {
    // Two passes writing the same image in the same usage still need a barrier between them.
    RecordingBackend backend;
    RenderGraph graph;
    const auto color = graph.createImage("color", kDesc);
    graph.addPass("first", [&](PassBuilder &b) { b.write(color, ImageUsage::ColorAttachment); }, mark(backend, "first"));
    graph.addPass("second", [&](PassBuilder &b) { b.write(color, ImageUsage::ColorAttachment); }, mark(backend, "second"));
    graph.addPass("readback", [&](PassBuilder &b) { b.read(color, ImageUsage::TransferSrc); b.sideEffects(); }, mark(backend, "readback"));
    graph.compile();
    CHECK(graph.compiledPasses()[1].transitions.size() == 1);
    CHECK(graph.compiledPasses()[1].transitions[0].from == ImageUsage::ColorAttachment);
    CHECK(graph.compiledPasses()[1].transitions[0].to == ImageUsage::ColorAttachment);
}

void readAfterReadHasNoBarrier() {
    RenderGraph graph;
    const auto color = graph.createImage("color", kDesc);
    graph.addPass("scene", [&](PassBuilder &b) { b.write(color, ImageUsage::ColorAttachment); }, nullptr);
    graph.addPass("r1", [&](PassBuilder &b) { b.read(color, ImageUsage::TransferSrc); b.sideEffects(); }, nullptr);
    graph.addPass("r2", [&](PassBuilder &b) { b.read(color, ImageUsage::TransferSrc); b.sideEffects(); }, nullptr);
    graph.compile();
    CHECK(graph.compiledPasses()[1].transitions.size() == 1);
    CHECK(graph.compiledPasses()[2].transitions.empty());
}

} // namespace

int main() {
    headlessGraph();
    presentGraph();
    cullsUnusedPasses();
    culledProducerChain();
    rejectsReadBeforeWrite();
    rejectsConflictingUsagesInOnePass();
    rejectsUnknownResource();
    writeAfterWriteGetsABarrier();
    readAfterReadHasNoBarrier();
    std::cout << "render graph tests passed" << std::endl;
    return 0;
}
