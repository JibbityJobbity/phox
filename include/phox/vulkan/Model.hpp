#ifndef PHOX_MODEL_HPP
#define PHOX_MODEL_HPP

#include "phox/Common.hpp"

#include <glm/glm.hpp>

#include "Device.hpp"
#include "Buffer.hpp"

namespace phox {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

class Model {
  private:
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    Device *m_device = nullptr;
    std::unique_ptr<Buffer> m_vertexBuffer;
    std::unique_ptr<Buffer> m_indexBuffer;

    Model() = default;

  public:
    static Model FromObj(Device &device, std::string &path);
    void draw(VkCommandBuffer commandBuffer);
    void initialize();
    void cleanup();
};

} // namespace phox

#endif // PHOX_MODEL_HPP
