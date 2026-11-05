#include "phox/ShaderInterface.hpp"

#include "phox/Scene.hpp"

namespace phox {

MaterialData materialData(const Material &material) {
    MaterialData data;
    data.baseColor = material.albedo;
    data.emissive = vec4(material.emissive, 0.0f);
    data.metallic = material.metallic;
    data.roughness = material.roughness;
    data.normalScale = material.normalScale;
    data.occlusionStrength = material.occlusionStrength;
    data.alphaCutoff = material.alphaCutoff;
    if (material.alphaMode == AlphaMode::Mask) {
        data.flags |= kMaterialAlphaMask;
    } else if (material.alphaMode == AlphaMode::Blend) {
        data.flags |= kMaterialAlphaBlend;
    }
    return data;
}

} // namespace phox
