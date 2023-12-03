#include "Pipeline/Shader.hpp"

#include <iostream>
#include <vector>

using namespace phox;
using namespace std;

Shader::Shader(VkDevice device, VkShaderModule shader)
    : m_device(device), m_shader(shader) {}

void Shader::cleanup() { vkDestroyShaderModule(m_device, m_shader, nullptr); }

VkShaderModule Shader::getShader() { return m_shader; }
