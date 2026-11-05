#ifndef PHOX_SCENE_MATERIAL_HPP
#define PHOX_SCENE_MATERIAL_HPP

#include <array>
#include <string>

namespace phox::scene {

struct Material {
    std::string name;
    std::string shaderName;
    std::array<float, 4> albedo{1.0f, 0.2f, 0.4f, 1.0f};
};

} // namespace phox::scene

#endif // PHOX_SCENE_MATERIAL_HPP
