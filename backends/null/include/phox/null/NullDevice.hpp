#ifndef PHOX_NULL_NULL_DEVICE_HPP
#define PHOX_NULL_NULL_DEVICE_HPP

#include "phox/rhi/Device.hpp"

#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace phox::null {

namespace detail {

// Slot pool whose handles carry a generation. Destroying a slot bumps its generation, so a stale
// handle never resolves again, even after the slot is reused.
template <class Handle, class T> class Pool {
  public:
    Handle create(T value) {
        uint32_t index;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
        } else {
            index = static_cast<uint32_t>(m_slots.size());
            m_slots.emplace_back();
        }
        Slot &slot = m_slots[index];
        slot.value = std::move(value);
        slot.alive = true;
        return Handle{index, slot.generation};
    }

    bool contains(Handle handle) const {
        return handle.valid() && handle.index < m_slots.size() && m_slots[handle.index].alive &&
               m_slots[handle.index].generation == handle.generation;
    }

    // Debug builds abort on a stale or invalid handle; release builds throw instead of invoking UB.
    T &get(Handle handle) {
        check(handle);
        return m_slots[handle.index].value;
    }
    const T &get(Handle handle) const {
        check(handle);
        return m_slots[handle.index].value;
    }

    // Invalid handle: no-op. Stale handle: assert in debug, no-op in release.
    void destroy(Handle handle) {
        if (!handle.valid()) {
            return;
        }
        if (!contains(handle)) {
            assert(false && "destroy() called with a stale handle");
            return;
        }
        Slot &slot = m_slots[handle.index];
        slot.value = T{};
        slot.alive = false;
        ++slot.generation;
        m_free.push_back(handle.index);
    }

  private:
    struct Slot {
        T value{};
        uint32_t generation = 0;
        bool alive = false;
    };

    void check(Handle handle) const {
        if (!contains(handle)) {
            assert(false && "stale or invalid handle");
            throw std::logic_error("stale or invalid handle");
        }
    }

    std::vector<Slot> m_slots;
    std::vector<uint32_t> m_free;
};

} // namespace detail

/// Records the name of every call. `beginDebugLabel` is recorded as "beginDebugLabel:<label>".
class NullCommandList final : public rhi::CommandList {
  public:
    explicit NullCommandList(const rhi::Capabilities &capabilities) : m_capabilities(capabilities) {}

    const std::vector<std::string> &recording() const { return m_recording; }
    std::vector<std::string> takeRecording() { return std::move(m_recording); }

    void barrier(std::span<const rhi::TextureBarrier>, std::span<const rhi::BufferBarrier>) override;
    void beginRendering(const rhi::RenderingInfo &) override;
    void endRendering() override;
    void bindPipeline(rhi::PipelineHandle) override;
    void bindVertexBuffer(rhi::BufferHandle, uint64_t offset) override;
    void bindIndexBuffer(rhi::BufferHandle, uint64_t offset) override;
    void bindUniformBuffer(uint32_t set, uint32_t binding, rhi::BufferHandle) override;
    void bindStorageBuffer(uint32_t set, uint32_t binding, rhi::BufferHandle) override;
    void bindSampledTexture(uint32_t set, uint32_t binding, rhi::TextureHandle) override;
    void pushConstants(rhi::ShaderStage, uint32_t offset, std::span<const std::byte>) override;
    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex) override;
    void drawMeshTasks(uint32_t x, uint32_t y, uint32_t z) override;
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override;
    void copyBuffer(rhi::BufferHandle src, rhi::BufferHandle dst, uint64_t size) override;
    void copyTextureToBuffer(rhi::TextureHandle src, rhi::BufferHandle dst) override;
    void beginDebugLabel(const char *name) override;
    void endDebugLabel() override;

  private:
    const rhi::Capabilities &m_capabilities;
    std::vector<std::string> m_recording;
};

/// A device that owns handles and descriptions but never touches a GPU. For testing graph logic.
class NullDevice final : public rhi::Device {
  public:
    NullDevice();

    /// Overrides the reported feature set (default: none).
    void setFeatures(std::bitset<32> features) { m_capabilities.features = features; }

    /// Call names recorded by the most recently submitted command list; empty before the first submit.
    const std::vector<std::string> &lastRecording() const { return m_lastRecording; }

    const rhi::Capabilities &capabilities() const override { return m_capabilities; }

    rhi::BufferHandle createBuffer(const rhi::BufferDesc &) override;
    rhi::TextureHandle createTexture(const rhi::TextureDesc &) override;
    rhi::ShaderHandle createShader(const rhi::ShaderDesc &) override;
    rhi::PipelineHandle createGraphicsPipeline(const rhi::GraphicsPipelineDesc &) override;
    rhi::PipelineHandle createComputePipeline(const rhi::ComputePipelineDesc &) override;
    void destroy(rhi::BufferHandle) override;
    void destroy(rhi::TextureHandle) override;
    void destroy(rhi::ShaderHandle) override;
    void destroy(rhi::PipelineHandle) override;

    std::span<std::byte> map(rhi::BufferHandle) override;
    const rhi::TextureDesc &describe(rhi::TextureHandle) const override;
    const rhi::BufferDesc &describe(rhi::BufferHandle) const override;

    rhi::CommandList &beginCommands() override;
    rhi::SubmitTicket submit(rhi::CommandList &) override;
    void wait(rhi::SubmitTicket) override {}
    void waitIdle() override {}

  private:
    struct BufferData {
        rhi::BufferDesc desc{};
        std::vector<std::byte> memory; // zero-filled; only for Upload / Readback buffers
    };
    struct TextureData {
        rhi::TextureDesc desc{};
    };
    struct ShaderData {
        rhi::ShaderStage stage{};
    };
    struct PipelineData {
        bool compute = false;
    };

    const char *keepName(const char *name);

    rhi::Capabilities m_capabilities;
    detail::Pool<rhi::BufferHandle, BufferData> m_buffers;
    detail::Pool<rhi::TextureHandle, TextureData> m_textures;
    detail::Pool<rhi::ShaderHandle, ShaderData> m_shaders;
    detail::Pool<rhi::PipelineHandle, PipelineData> m_pipelines;
    std::deque<std::string> m_names; // stable storage so describe() can hand back debug names
    std::unique_ptr<NullCommandList> m_current;
    std::vector<std::string> m_lastRecording;
    uint64_t m_ticket = 0;
};

} // namespace phox::null

#endif // PHOX_NULL_NULL_DEVICE_HPP
