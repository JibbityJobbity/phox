#ifndef PHOX_DEVICE_HPP
#define PHOX_DEVICE_HPP

#include "phox/Common.hpp"

#include "phox/Buffer.hpp"
#include "phox/Image.hpp"
#include "phox/pipeline/Pipeline.hpp"

namespace phox {

enum class MemoryLocation { Auto, Device, Host, HostVisible };

enum class DeviceType { Discrete, Integrated, Virtual, CPU, Unknown };

class Device {
  public:
    virtual ~Device() = default;
    virtual std::unique_ptr<Buffer> allocateBuffer(const size_t size, const MemoryLocation memoryLocation) = 0;
    virtual std::unique_ptr<Image> allocateImage(size_t width, size_t height) = 0;
    virtual std::unique_ptr<Buffer> copyBuffer(Buffer &buffer, MemoryLocation memoryLocation) = 0;
    /**
     * @brief Create a Pipeline
     *
     * Creates a new pipeline based on the provided shaders. Duplicate shader types are prohibited.
     * To create a graphics pipeline, provide a shader of both a Vertex and Fragment shader.
     *
     * @param shaders
     * @return std::unique_ptr<Pipeline>
     */
    virtual std::unique_ptr<Pipeline> createPipeline(const std::vector<Shader*>& shaders) = 0;
    virtual void freeBuffer(Buffer &buffer) = 0;
    virtual bool isInitialized() = 0;
    virtual void cleanup() = 0;
};

}

#endif // PHOX_DEVICE_HPP
