#include "phox/null/NullDevice.hpp"

namespace phox::null {

void NullCommandList::barrier(std::span<const rhi::TextureBarrier>, std::span<const rhi::BufferBarrier>) {
    m_recording.emplace_back("barrier");
}
void NullCommandList::beginRendering(const rhi::RenderingInfo &) { m_recording.emplace_back("beginRendering"); }
void NullCommandList::endRendering() { m_recording.emplace_back("endRendering"); }
void NullCommandList::bindPipeline(rhi::PipelineHandle) { m_recording.emplace_back("bindPipeline"); }
void NullCommandList::bindVertexBuffer(rhi::BufferHandle, uint64_t) { m_recording.emplace_back("bindVertexBuffer"); }
void NullCommandList::bindIndexBuffer(rhi::BufferHandle, uint64_t) { m_recording.emplace_back("bindIndexBuffer"); }
void NullCommandList::bindUniformBuffer(uint32_t, uint32_t, rhi::BufferHandle) {
    m_recording.emplace_back("bindUniformBuffer");
}
void NullCommandList::bindStorageBuffer(uint32_t, uint32_t, rhi::BufferHandle) {
    m_recording.emplace_back("bindStorageBuffer");
}
void NullCommandList::bindSampledTexture(uint32_t, uint32_t, rhi::TextureHandle) {
    m_recording.emplace_back("bindSampledTexture");
}
void NullCommandList::pushConstants(rhi::ShaderStage, uint32_t, std::span<const std::byte>) {
    m_recording.emplace_back("pushConstants");
}
void NullCommandList::draw(uint32_t, uint32_t, uint32_t) { m_recording.emplace_back("draw"); }
void NullCommandList::drawIndexed(uint32_t, uint32_t, uint32_t) { m_recording.emplace_back("drawIndexed"); }
void NullCommandList::drawMeshTasks(uint32_t, uint32_t, uint32_t) {
    assert(m_capabilities.has(rhi::Feature::MeshShader) && "drawMeshTasks requires Feature::MeshShader");
    m_recording.emplace_back("drawMeshTasks");
}
void NullCommandList::dispatch(uint32_t, uint32_t, uint32_t) { m_recording.emplace_back("dispatch"); }
void NullCommandList::copyBuffer(rhi::BufferHandle, rhi::BufferHandle, uint64_t) {
    m_recording.emplace_back("copyBuffer");
}
void NullCommandList::copyTextureToBuffer(rhi::TextureHandle, rhi::BufferHandle) {
    m_recording.emplace_back("copyTextureToBuffer");
}
void NullCommandList::beginDebugLabel(const char *name) {
    m_recording.push_back(std::string("beginDebugLabel:") + (name ? name : ""));
}
void NullCommandList::endDebugLabel() { m_recording.emplace_back("endDebugLabel"); }

} // namespace phox::null
