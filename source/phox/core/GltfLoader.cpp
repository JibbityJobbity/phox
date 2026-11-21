#include "phox/GltfLoader.hpp"

#include "phox/Log.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

// Static, so these cannot clash with another copy of stb_image linked into the program (assimp has one).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace phox {

namespace {

// Extensions whose absence of support only loses fidelity; anything else in extensionsRequired is fatal.
constexpr std::array<std::string_view, 2> kSupportedExtensions = {"KHR_materials_emissive_strength",
                                                                  "KHR_mesh_quantization"};

std::vector<std::uint8_t> decodeBase64(std::string_view text) {
    std::array<int, 256> table{};
    table.fill(-1);
    const std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t i = 0; i < alphabet.size(); ++i) table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
    std::vector<std::uint8_t> out;
    out.reserve(text.size() * 3 / 4);
    std::uint32_t bits = 0;
    int count = 0;
    for (char c : text) {
        const int value = table[static_cast<unsigned char>(c)];
        if (value < 0) {
            continue; // padding and whitespace
        }
        bits = (bits << 6) | static_cast<std::uint32_t>(value);
        count += 6;
        if (count >= 8) {
            count -= 8;
            out.push_back(static_cast<std::uint8_t>((bits >> count) & 0xFFu));
        }
    }
    return out;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

TextureFilter toFilter(int gl, TextureFilter fallback) {
    switch (gl) {
    case 9728: // NEAREST
    case 9984: // NEAREST_MIPMAP_NEAREST
    case 9986: // NEAREST_MIPMAP_LINEAR
        return TextureFilter::Nearest;
    case 9729: // LINEAR
    case 9985: // LINEAR_MIPMAP_NEAREST
    case 9987: // LINEAR_MIPMAP_LINEAR
        return TextureFilter::Linear;
    default:
        return fallback;
    }
}

TextureWrap toWrap(int gl) {
    switch (gl) {
    case 33071: return TextureWrap::ClampToEdge;
    case 33648: return TextureWrap::MirroredRepeat;
    default: return TextureWrap::Repeat; // 10497, and the default when unset
    }
}

mat4 toMat4(const cgltf_float m[16]) {
    mat4 result;
    std::memcpy(glm::value_ptr(result), m, sizeof(float) * 16); // both column-major
    return result;
}

class Loader {
public:
    Loader(const std::filesystem::path &path, Scene &scene, GltfImport &result)
        : m_path(path), m_scene(scene), m_result(result) {}

    ~Loader() {
        if (m_data != nullptr) {
            cgltf_free(m_data);
        }
    }

    void load(const mat4 &root) {
        cgltf_options options{};
        const std::string file = m_path.string();
        if (cgltf_parse_file(&options, file.c_str(), &m_data) != cgltf_result_success) {
            throw std::runtime_error("cannot parse glTF file " + file);
        }
        if (cgltf_load_buffers(&options, m_data, file.c_str()) != cgltf_result_success) {
            throw std::runtime_error("cannot load the buffers of " + file);
        }
        if (cgltf_validate(m_data) != cgltf_result_success) {
            throw std::runtime_error(file + " is not valid glTF");
        }
        checkExtensions();
        if (m_data->skins_count > 0) warn("skins are ignored");
        if (m_data->animations_count > 0) warn("animations are ignored");
        if (m_data->cameras_count > 0) warn("cameras are ignored");
        if (m_data->lights_count > 0) warn("lights (KHR_lights_punctual) are ignored");

        const cgltf_scene *gltfScene = m_data->scene != nullptr ? m_data->scene
                                       : m_data->scenes_count > 0 ? &m_data->scenes[0]
                                                                  : nullptr;
        if (gltfScene != nullptr) {
            for (cgltf_size i = 0; i < gltfScene->nodes_count; ++i) addNode(*gltfScene->nodes[i], root);
        } else {
            // No scene: every root node is shown.
            for (cgltf_size i = 0; i < m_data->nodes_count; ++i) {
                if (m_data->nodes[i].parent == nullptr) addNode(m_data->nodes[i], root);
            }
        }
        m_result.meshes = m_meshes.size();
        m_result.materials = m_materials.size();
        m_result.textures = m_textures.size();
    }

private:
    void warn(std::string message) {
        if (std::find(m_result.warnings.begin(), m_result.warnings.end(), message) == m_result.warnings.end()) {
            log::warn("glTF {}: {}", m_path.filename().string(), message);
            m_result.warnings.push_back(std::move(message));
        }
    }

    void checkExtensions() {
        const auto supported = [](const char *name) {
            return std::find(kSupportedExtensions.begin(), kSupportedExtensions.end(), std::string_view(name)) !=
                   kSupportedExtensions.end();
        };
        for (cgltf_size i = 0; i < m_data->extensions_required_count; ++i) {
            if (!supported(m_data->extensions_required[i])) {
                throw std::runtime_error(m_path.string() + " requires the unsupported extension " +
                                         m_data->extensions_required[i]);
            }
        }
        for (cgltf_size i = 0; i < m_data->extensions_used_count; ++i) {
            if (!supported(m_data->extensions_used[i])) {
                warn(std::string("extension ") + m_data->extensions_used[i] + " is ignored");
            }
        }
    }

    void addNode(const cgltf_node &node, const mat4 &parent) {
        cgltf_float matrix[16];
        cgltf_node_transform_local(&node, matrix);
        const mat4 world = parent * toMat4(matrix);
        if (node.mesh != nullptr) {
            for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p) {
                const cgltf_primitive &primitive = node.mesh->primitives[p];
                std::shared_ptr<const Mesh> mesh = meshFor(primitive);
                if (!mesh) {
                    continue;
                }
                std::string name = node.name != nullptr ? node.name : node.mesh->name != nullptr ? node.mesh->name : "node";
                if (node.mesh->primitives_count > 1) {
                    name += " " + std::to_string(p);
                }
                const Bounds local = mesh->bounds();
                for (int corner = 0; corner < 8; ++corner) {
                    const vec3 point{(corner & 1) ? local.max.x : local.min.x, (corner & 2) ? local.max.y : local.min.y,
                                     (corner & 4) ? local.max.z : local.min.z};
                    const vec3 w = vec3(world * vec4(point, 1.0f));
                    if (m_result.objects.empty() && corner == 0) {
                        m_result.bounds = {w, w};
                    }
                    m_result.bounds.min = glm::min(m_result.bounds.min, w);
                    m_result.bounds.max = glm::max(m_result.bounds.max, w);
                }
                m_result.objects.push_back(
                    m_scene.addObject({.mesh = std::move(mesh), .material = materialFor(primitive.material),
                                       .transform = Transform::fromMatrix(world), .name = std::move(name)}));
            }
        }
        for (cgltf_size i = 0; i < node.children_count; ++i) addNode(*node.children[i], world);
    }

    std::shared_ptr<const Mesh> meshFor(const cgltf_primitive &primitive) {
        if (const auto it = m_meshes.find(&primitive); it != m_meshes.end()) {
            return it->second;
        }
        std::shared_ptr<const Mesh> mesh = buildMesh(primitive);
        m_meshes.emplace(&primitive, mesh);
        return mesh;
    }

    std::shared_ptr<const Mesh> buildMesh(const cgltf_primitive &primitive) {
        if (primitive.type != cgltf_primitive_type_triangles && primitive.type != cgltf_primitive_type_triangle_strip &&
            primitive.type != cgltf_primitive_type_triangle_fan) {
            warn("points and lines are skipped");
            return nullptr;
        }
        if (primitive.targets_count > 0) {
            warn("morph targets are ignored");
        }
        const cgltf_accessor *positions = nullptr;
        const cgltf_accessor *normals = nullptr;
        const cgltf_accessor *texcoords = nullptr;
        const cgltf_accessor *tangents = nullptr;
        for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
            const cgltf_attribute &attribute = primitive.attributes[i];
            switch (attribute.type) {
            case cgltf_attribute_type_position: positions = attribute.data; break;
            case cgltf_attribute_type_normal: normals = attribute.data; break;
            case cgltf_attribute_type_tangent: tangents = attribute.data; break;
            case cgltf_attribute_type_texcoord:
                if (attribute.index == 0) texcoords = attribute.data;
                break;
            default: break;
            }
        }
        if (positions == nullptr || positions->count == 0) {
            return nullptr;
        }

        std::vector<Vertex> vertices(positions->count);
        for (cgltf_size i = 0; i < positions->count; ++i) {
            Vertex &v = vertices[i];
            cgltf_accessor_read_float(positions, i, glm::value_ptr(v.position), 3);
            if (normals != nullptr) cgltf_accessor_read_float(normals, i, glm::value_ptr(v.normal), 3);
            if (texcoords != nullptr) cgltf_accessor_read_float(texcoords, i, glm::value_ptr(v.uv), 2);
            if (tangents != nullptr) cgltf_accessor_read_float(tangents, i, glm::value_ptr(v.tangent), 4);
        }

        // The primitive's vertex order as a triangle list.
        std::vector<std::uint32_t> order;
        const cgltf_size count = primitive.indices != nullptr ? primitive.indices->count : positions->count;
        const auto at = [&](cgltf_size i) {
            return primitive.indices != nullptr ? static_cast<std::uint32_t>(cgltf_accessor_read_index(primitive.indices, i))
                                                : static_cast<std::uint32_t>(i);
        };
        if (primitive.type == cgltf_primitive_type_triangles) {
            for (cgltf_size i = 0; i + 2 < count; i += 3) {
                order.insert(order.end(), {at(i), at(i + 1), at(i + 2)});
            }
        } else if (primitive.type == cgltf_primitive_type_triangle_strip) {
            for (cgltf_size i = 0; i + 2 < count; ++i) {
                // Every other triangle is reversed to keep the winding.
                if (i % 2 == 0) {
                    order.insert(order.end(), {at(i), at(i + 1), at(i + 2)});
                } else {
                    order.insert(order.end(), {at(i + 1), at(i), at(i + 2)});
                }
            }
        } else {
            for (cgltf_size i = 1; i + 1 < count; ++i) {
                order.insert(order.end(), {at(i), at(i + 1), at(0)});
            }
        }
        if (order.empty()) {
            return nullptr;
        }
        if (normals == nullptr) {
            // Flat normals, as the spec asks: every triangle gets its own vertices.
            std::vector<Vertex> flat;
            flat.reserve(order.size());
            for (std::size_t t = 0; t + 2 < order.size(); t += 3) {
                std::array<Vertex, 3> corners{vertices[order[t]], vertices[order[t + 1]], vertices[order[t + 2]]};
                const vec3 cross = glm::cross(corners[1].position - corners[0].position,
                                              corners[2].position - corners[0].position);
                const float length = glm::length(cross);
                const vec3 normal = length > 0.0f ? cross / length : vec3(0.0f, 0.0f, 1.0f);
                for (Vertex &corner : corners) {
                    corner.normal = normal;
                    corner.tangent = vec4(0.0f);
                    flat.push_back(corner);
                }
            }
            vertices = std::move(flat);
            order.resize(vertices.size());
            for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<std::uint32_t>(i);
        }
        return std::make_shared<Mesh>(std::move(vertices), std::move(order));
    }

    Material materialFor(const cgltf_material *source) {
        if (const auto it = m_materials.find(source); it != m_materials.end()) {
            return it->second;
        }
        Material material;
        if (source == nullptr) {
            // The spec's default material.
            material = Material{.name = "default", .albedo = vec4(1.0f), .metallic = 1.0f, .roughness = 1.0f,
                                .doubleSided = false};
        } else {
            material.name = source->name != nullptr ? source->name : "material";
            if (source->has_pbr_metallic_roughness) {
                const cgltf_pbr_metallic_roughness &pbr = source->pbr_metallic_roughness;
                material.albedo = glm::make_vec4(pbr.base_color_factor);
                material.metallic = pbr.metallic_factor;
                material.roughness = pbr.roughness_factor;
                material.baseColorTexture = textureFor(pbr.base_color_texture, TextureEncoding::Srgb);
                material.metallicRoughnessTexture = textureFor(pbr.metallic_roughness_texture, TextureEncoding::Linear);
            } else {
                material.albedo = vec4(1.0f);
                material.metallic = 1.0f;
                material.roughness = 1.0f;
            }
            material.normalTexture = textureFor(source->normal_texture, TextureEncoding::Normal);
            material.normalScale = source->normal_texture.texture != nullptr ? source->normal_texture.scale : 1.0f;
            material.occlusionTexture = textureFor(source->occlusion_texture, TextureEncoding::Linear);
            material.occlusionStrength = source->occlusion_texture.texture != nullptr ? source->occlusion_texture.scale : 1.0f;
            material.emissive = glm::make_vec3(source->emissive_factor);
            if (source->has_emissive_strength) {
                material.emissive *= source->emissive_strength.emissive_strength;
            }
            material.emissiveTexture = textureFor(source->emissive_texture, TextureEncoding::Srgb);
            material.alphaMode = source->alpha_mode == cgltf_alpha_mode_mask    ? AlphaMode::Mask
                                 : source->alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend
                                                                                : AlphaMode::Opaque;
            material.alphaCutoff = source->alpha_cutoff;
            material.doubleSided = source->double_sided != 0;
        }
        m_materials.emplace(source, material);
        return material;
    }

    std::shared_ptr<const Texture> textureFor(const cgltf_texture_view &view, TextureEncoding encoding) {
        if (view.texture == nullptr) {
            return nullptr;
        }
        if (view.texcoord != 0) {
            warn("textures on texture coordinates other than TEXCOORD_0 use TEXCOORD_0");
        }
        if (view.has_transform) {
            warn("KHR_texture_transform is ignored");
        }
        const auto key = std::make_pair(view.texture, encoding);
        if (const auto it = m_textures.find(key); it != m_textures.end()) {
            return it->second;
        }
        std::shared_ptr<const Texture> texture;
        const cgltf_image *image = view.texture->image;
        if (image == nullptr) {
            warn("textures without a PNG or JPEG image (e.g. KTX2 or WebP) are ignored");
        } else {
            SamplerDesc sampler;
            bool mipmaps = true;
            if (const cgltf_sampler *s = view.texture->sampler) {
                sampler.magFilter = toFilter(static_cast<int>(s->mag_filter), TextureFilter::Linear);
                sampler.minFilter = toFilter(static_cast<int>(s->min_filter), TextureFilter::Linear);
                const int min = static_cast<int>(s->min_filter);
                sampler.mipFilter = min == 9984 || min == 9985 ? TextureFilter::Nearest : TextureFilter::Linear;
                mipmaps = min == 0 || min >= 9984; // unset, or a mipmap mode
                sampler.wrapU = toWrap(static_cast<int>(s->wrap_s));
                sampler.wrapV = toWrap(static_cast<int>(s->wrap_t));
            }
            texture = decode(*image, encoding, sampler, mipmaps);
        }
        m_textures.emplace(key, texture);
        return texture;
    }

    std::shared_ptr<const Texture> decode(const cgltf_image &image, TextureEncoding encoding, const SamplerDesc &sampler,
                                          bool mipmaps) {
        std::vector<std::uint8_t> bytes;
        if (image.buffer_view != nullptr) {
            const auto *start = static_cast<const std::uint8_t *>(cgltf_buffer_view_data(image.buffer_view));
            bytes.assign(start, start + image.buffer_view->size);
        } else if (image.uri != nullptr && std::strncmp(image.uri, "data:", 5) == 0) {
            const char *comma = std::strchr(image.uri, ',');
            if (comma == nullptr) {
                warn("an image has a malformed data URI");
                return nullptr;
            }
            bytes = decodeBase64(comma + 1);
        } else if (image.uri != nullptr) {
            std::string uri = image.uri;
            cgltf_decode_uri(uri.data());
            uri.resize(std::strlen(uri.c_str()));
            // URIs are UTF-8.
            const std::u8string utf8(uri.begin(), uri.end());
            bytes = readFile(m_path.parent_path() / std::filesystem::path(utf8));
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc *pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
        if (pixels == nullptr) {
            warn(std::string("an image could not be decoded (") + stbi_failure_reason() + ")");
            return nullptr;
        }
        std::vector<std::uint8_t> rgba(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
        stbi_image_free(pixels);
        const std::string name = image.name != nullptr ? image.name : image.uri != nullptr && std::strncmp(image.uri, "data:", 5) != 0 ? image.uri : "image";
        return std::make_shared<Texture>(name, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                                         std::move(rgba), encoding, sampler, mipmaps);
    }

    std::filesystem::path m_path;
    Scene &m_scene;
    GltfImport &m_result;
    cgltf_data *m_data = nullptr;
    std::unordered_map<const cgltf_primitive *, std::shared_ptr<const Mesh>> m_meshes;
    std::unordered_map<const cgltf_material *, Material> m_materials;
    std::map<std::pair<const cgltf_texture *, TextureEncoding>, std::shared_ptr<const Texture>> m_textures;
};

} // namespace

GltfImport loadGltf(const std::filesystem::path &path, Scene &scene, const mat4 &root) {
    GltfImport result;
    Loader loader(path, scene, result);
    loader.load(root);
    log::debug("glTF {}: {} object(s), {} mesh(es), {} material(s), {} texture(s)", path.filename().string(),
               result.objects.size(), result.meshes, result.materials, result.textures);
    return result;
}

} // namespace phox
