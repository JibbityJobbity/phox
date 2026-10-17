#ifndef PHOX_RHI_DEVICE_HPP
#define PHOX_RHI_DEVICE_HPP

#include "phox/rhi/Capabilities.hpp"
#include "phox/rhi/CommandList.hpp"
#include "phox/rhi/Descs.hpp"
#include "phox/rhi/Handles.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace phox::rhi {

struct SubmitTicket {
    uint64_t value = 0;
}; // timeline semaphore value

class Device {
  public:
    virtual ~Device() = default;
    virtual const Capabilities &capabilities() const = 0;

    virtual BufferHandle createBuffer(const BufferDesc &) = 0;
    virtual TextureHandle createTexture(const TextureDesc &) = 0;
    virtual ShaderHandle createShader(const ShaderDesc &) = 0;
    virtual PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc &) = 0;
    virtual PipelineHandle createComputePipeline(const ComputePipelineDesc &) = 0;
    virtual void destroy(BufferHandle) = 0; // must: deferred until the GPU is done with it
    virtual void destroy(TextureHandle) = 0;
    virtual void destroy(ShaderHandle) = 0;
    virtual void destroy(PipelineHandle) = 0;

    virtual std::span<std::byte> map(BufferHandle) = 0; // must: only Upload / Readback memory; persistent map
    virtual const TextureDesc &describe(TextureHandle) const = 0;
    virtual const BufferDesc &describe(BufferHandle) const = 0;

    virtual CommandList &beginCommands() = 0; // one list per call; valid until submit()
    virtual SubmitTicket submit(CommandList &) = 0;
    virtual void wait(SubmitTicket) = 0;
    virtual void waitIdle() = 0;
};

} // namespace phox::rhi

#endif // PHOX_RHI_DEVICE_HPP
