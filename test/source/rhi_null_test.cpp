#include "Check.hpp"

#include "phox/Log.hpp"
#include "phox/null/NullDevice.hpp"
#include "phox/rhi/Backend.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(NDEBUG) && defined(__unix__)
#include <csignal>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>
#define PHOX_TEST_DEATH 1
#endif

using namespace phox;

namespace {

rhi::BufferDesc uploadBuffer(uint64_t size) {
    return {size, rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDst, rhi::MemoryKind::Upload, "test"};
}

rhi::TextureDesc colorTexture(uint32_t w, uint32_t h) {
    return {w, h, 1, rhi::Format::RGBA8Unorm, rhi::TextureUsage::ColorAttachment, "target"};
}

void testSlotReuse() {
    null::NullDevice device;
    const auto first = device.createBuffer(uploadBuffer(64));
    CHECK(first.valid());
    device.destroy(first);
    const auto second = device.createBuffer(uploadBuffer(64));
    CHECK(second.index == first.index);
    CHECK(second.generation == first.generation + 1);
    CHECK(first != second);

    // A second live buffer gets a different slot.
    const auto third = device.createBuffer(uploadBuffer(16));
    CHECK(third.index != second.index);
}

void testInvalidDestroyIsNoOp() {
    null::NullDevice device;
    device.destroy(rhi::BufferHandle{}); // invalid handle: no-op
    device.destroy(rhi::TextureHandle{});
}

void testStaleHandle() {
    null::NullDevice device;
    const auto handle = device.createBuffer(uploadBuffer(8));
    device.destroy(handle);
#ifdef PHOX_TEST_DEATH
    // Debug build: describe() on a stale handle asserts.
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        if (std::freopen("/dev/null", "w", stderr) == nullptr) {
            _exit(2);
        }
        std::signal(SIGABRT, SIG_DFL);
        (void)device.describe(handle);
        _exit(0); // not reached when the assert fires
    }
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
    // Release build: asserts are compiled out, so it throws instead of invoking undefined behaviour.
    CHECK_THROWS_WITH(device.describe(handle), std::logic_error, "stale");
#endif
}

void testVulkanUnavailable() {
#ifndef PHOX_TEST_VULKAN_ENABLED
    CHECK_THROWS_WITH(rhi::createDevice(rhi::Backend::Vulkan), std::runtime_error, "Vulkan");
#endif
}

void testCreateNullDevice() {
    const auto device = rhi::createDevice(rhi::Backend::Null);
    CHECK(device != nullptr);
    CHECK(device->capabilities().deviceName == "Null");
    for (const auto feature : {rhi::Feature::MeshShader, rhi::Feature::RayTracing}) {
        CHECK(!device->capabilities().has(feature));
    }
}

void testFeatures() {
    null::NullDevice device;
    std::bitset<32> features;
    features.set(static_cast<size_t>(rhi::Feature::MeshShader));
    device.setFeatures(features);
    CHECK(device.capabilities().has(rhi::Feature::MeshShader));
    CHECK(!device.capabilities().has(rhi::Feature::RayTracing));
    CHECK(rhi::toString(rhi::Feature::MeshShader) == "MeshShader");
}

void testDescribeAndMap() {
    null::NullDevice device;
    const auto buffer = device.createBuffer(uploadBuffer(32));
    CHECK(device.describe(buffer).size == 32);
    CHECK(std::string(device.describe(buffer).debugName) == "test");
    const auto bytes = device.map(buffer);
    CHECK(bytes.size() == 32);
    for (const std::byte b : bytes) {
        CHECK(b == std::byte{0});
    }

    const auto texture = device.createTexture(colorTexture(640, 480));
    CHECK(device.describe(texture).width == 640 && device.describe(texture).height == 480);

    CHECK_THROWS_WITH(device.createBuffer(uploadBuffer(0)), std::invalid_argument, "size");
    CHECK_THROWS_WITH(device.createTexture(colorTexture(0, 4)), std::invalid_argument, "width");
}

void testRecording() {
    null::NullDevice device;
    CHECK(device.lastRecording().empty());

    rhi::CommandList &commands = device.beginCommands();
    commands.beginDebugLabel("pass");
    commands.draw(3);
    commands.endDebugLabel();
    const auto ticket = device.submit(commands);
    device.wait(ticket);

    const std::vector<std::string> expected{"beginDebugLabel:pass", "draw", "endDebugLabel"};
    CHECK(device.lastRecording() == expected);
    CHECK(ticket.value == 1);

    // A list that is not the latest beginCommands() result cannot be submitted.
    rhi::CommandList &next = device.beginCommands();
    (void)device.submit(next);
    CHECK_THROWS_WITH(device.submit(next), std::logic_error, "beginCommands");
}

void testPipelines() {
    null::NullDevice device;
    const std::vector<uint32_t> code{0x07230203, 0, 0, 0};
    const auto vs = device.createShader({rhi::ShaderStage::Vertex, code});
    const auto fs = device.createShader({rhi::ShaderStage::Fragment, code});
    const auto cs = device.createShader({rhi::ShaderStage::Compute, code});

    rhi::GraphicsPipelineDesc desc;
    desc.shaders = {vs, fs};
    CHECK(device.createGraphicsPipeline(desc).valid());

    desc.shaders = {vs};
    CHECK_THROWS_WITH(device.createGraphicsPipeline(desc), std::invalid_argument, "Fragment");
    desc.shaders = {vs, vs, fs};
    CHECK_THROWS_WITH(device.createGraphicsPipeline(desc), std::invalid_argument, "duplicate");

    CHECK(device.createComputePipeline({cs, {}, {}, nullptr}).valid());
    CHECK_THROWS_WITH(device.createComputePipeline({vs, {}, {}, nullptr}), std::invalid_argument, "Compute");
    CHECK_THROWS_WITH(device.createShader({rhi::ShaderStage::Vertex, {}}), std::invalid_argument, "spirv");
}

std::vector<std::string> g_logged;
void captureLog(LogLevel level, std::string_view message) {
    g_logged.push_back(std::to_string(static_cast<int>(level)) + ":" + std::string(message));
}

void testLogCallback() {
    setLogCallback(captureLog);
    log(LogLevel::Info, "hello");
    CHECK(g_logged.size() == 1 && g_logged[0] == "1:hello");
    setLogCallback(nullptr);
    log(LogLevel::Debug, "dropped by the default sink"); // default sink ignores below Warning
    CHECK(g_logged.size() == 1);
}

} // namespace

int main() {
    testSlotReuse();
    testInvalidDestroyIsNoOp();
    testStaleHandle();
    testVulkanUnavailable();
    testCreateNullDevice();
    testFeatures();
    testDescribeAndMap();
    testRecording();
    testPipelines();
    testLogCallback();
    std::cout << "rhi null test passed" << std::endl;
    return 0;
}
