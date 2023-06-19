#ifndef PHOX_MODEL_HPP
#define PHOX_MODEL_HPP

#include "Common.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Buffer.hpp"
#define VK_NO_PROTOTYPES
#include "Device.hpp"

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

    Model() = default;
    ~Model() = default;

  public:
    static Model FromObj(Device &device, std::string &path);
    void draw(VkCommandBuffer commandBuffer);
    void initialize();
    void cleanup();
};

} // namespace phox

#endif // PHOX_MODEL_HPP
