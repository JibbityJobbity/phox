#ifndef PHOX_SHADER_HPP
#define PHOX_SHADER_HPP

namespace phox {

enum class ShaderType {
    VERTEX,
    FRAGMENT,
    COMPUTE
};

class Shader {
    public:
        const ShaderType type;
        Shader(const ShaderType type);
        virtual ~Shader() = default;
};

}

#endif // PHOX_SHADER_HPP
