#include "Internal.hpp"

#include <algorithm>
#include <cassert>
#include <sstream>

namespace phox::graph {

using namespace detail;

namespace {

std::string label(const std::string &name, const char *kind, uint32_t id) {
    return name.empty() ? std::string(kind) + "#" + std::to_string(id) : "'" + name + "'";
}

constexpr uint64_t kPoolRetireFrames = 3;

} // namespace

// ---------------------------------------------------------------------------------------------
// GraphBuilder
// ---------------------------------------------------------------------------------------------

GraphTexture GraphBuilder::createTexture(const rhi::TextureDesc &desc) {
    TextureNode node;
    node.desc = desc;
    node.name = desc.debugName != nullptr ? desc.debugName : "";
    node.desc.debugName = nullptr;
    m_graph->textures.push_back(std::move(node));
    return GraphTexture{static_cast<uint32_t>(m_graph->textures.size() - 1)};
}

GraphBuffer GraphBuilder::createBuffer(const rhi::BufferDesc &desc) {
    BufferNode node;
    node.desc = desc;
    node.name = desc.debugName != nullptr ? desc.debugName : "";
    node.desc.debugName = nullptr;
    m_graph->buffers.push_back(std::move(node));
    return GraphBuffer{static_cast<uint32_t>(m_graph->buffers.size() - 1)};
}

GraphTexture GraphBuilder::importTexture(rhi::TextureHandle handle, rhi::ResourceState before, rhi::ResourceState after) {
    if (!handle.valid()) {
        throw GraphError("importTexture: invalid texture handle");
    }
    TextureNode node;
    node.desc = m_device->describe(handle);
    node.name = node.desc.debugName != nullptr ? node.desc.debugName : "";
    node.desc.debugName = nullptr;
    node.imported = true;
    node.handle = handle;
    node.before = before;
    node.after = after;
    m_graph->textures.push_back(std::move(node));
    return GraphTexture{static_cast<uint32_t>(m_graph->textures.size() - 1)};
}

GraphBuffer GraphBuilder::importBuffer(rhi::BufferHandle handle, rhi::ResourceState before, rhi::ResourceState after) {
    if (!handle.valid()) {
        throw GraphError("importBuffer: invalid buffer handle");
    }
    BufferNode node;
    node.desc = m_device->describe(handle);
    node.name = node.desc.debugName != nullptr ? node.desc.debugName : "";
    node.desc.debugName = nullptr;
    node.imported = true;
    node.handle = handle;
    node.before = before;
    node.after = after;
    m_graph->buffers.push_back(std::move(node));
    return GraphBuffer{static_cast<uint32_t>(m_graph->buffers.size() - 1)};
}

void GraphBuilder::require(rhi::Feature feature) {
    if (std::find(m_graph->required.begin(), m_graph->required.end(), feature) == m_graph->required.end()) {
        m_graph->required.push_back(feature);
    }
}

void GraphBuilder::addPass(std::string name, std::function<void(PassBuilder &)> setup,
                           std::function<void(PassContext &)> execute) {
    m_graph->passes.emplace_back();
    try {
        PassNode &pass = m_graph->passes.back();
        pass.name = std::move(name);
        pass.execute = std::move(execute);
        PassBuilder builder(*m_graph, pass);
        if (setup) {
            setup(builder); // must not call addPass: that would invalidate `pass`
        }
    } catch (...) {
        m_graph->passes.pop_back();
        throw;
    }
}

// ---------------------------------------------------------------------------------------------
// PassBuilder
// ---------------------------------------------------------------------------------------------

namespace {

void checkTexture(const GraphData &graph, const PassNode &pass, GraphTexture t) {
    if (!t.valid() || t.id >= graph.textures.size()) {
        throw GraphError("pass '" + pass.name + "' uses an invalid texture handle");
    }
}

void checkBuffer(const GraphData &graph, const PassNode &pass, GraphBuffer b) {
    if (!b.valid() || b.id >= graph.buffers.size()) {
        throw GraphError("pass '" + pass.name + "' uses an invalid buffer handle");
    }
}

} // namespace

void PassBuilder::colorTarget(GraphTexture texture, bool clear, std::array<float, 4> clearColor) {
    checkTexture(m_graph, m_pass, texture);
    m_pass.colors.push_back({texture.id, clear, clearColor});
    m_pass.textures.push_back({texture.id, rhi::ResourceState::ColorAttachment, rhi::TextureUsage::ColorAttachment, true});
}

void PassBuilder::depthTarget(GraphTexture texture, bool clear, float clearDepth) {
    checkTexture(m_graph, m_pass, texture);
    if (m_pass.hasDepth) {
        throw GraphError("pass '" + m_pass.name + "' declares more than one depth target");
    }
    m_pass.hasDepth = true;
    m_pass.depthId = texture.id;
    m_pass.depthClear = clear;
    m_pass.depthValue = clearDepth;
    m_pass.textures.push_back({texture.id, rhi::ResourceState::DepthWrite, rhi::TextureUsage::DepthAttachment, true});
}

void PassBuilder::read(GraphTexture texture, TextureAccess access) {
    checkTexture(m_graph, m_pass, texture);
    if (access != TextureAccess::ShaderRead && access != TextureAccess::TransferSrc) {
        throw GraphError("pass '" + m_pass.name + "': read() takes ShaderRead or TransferSrc; use write() for writes");
    }
    m_pass.textures.push_back({texture.id, stateOf(access), usageOf(access), false});
}

void PassBuilder::write(GraphTexture texture, TextureAccess access) {
    checkTexture(m_graph, m_pass, texture);
    if (access != TextureAccess::ShaderWrite && access != TextureAccess::TransferDst) {
        throw GraphError("pass '" + m_pass.name + "': write() takes ShaderWrite or TransferDst; use read() for reads");
    }
    m_pass.textures.push_back({texture.id, stateOf(access), usageOf(access), true});
}

void PassBuilder::read(GraphBuffer buffer, BufferAccess access) {
    checkBuffer(m_graph, m_pass, buffer);
    if (access == BufferAccess::ShaderWrite || access == BufferAccess::TransferDst || access == BufferAccess::HostWrite) {
        throw GraphError("pass '" + m_pass.name + "': read() was given a write access; use write()");
    }
    m_pass.buffers.push_back({buffer.id, stateOf(access), usageOf(access), false, false});
}

void PassBuilder::write(GraphBuffer buffer, BufferAccess access) {
    checkBuffer(m_graph, m_pass, buffer);
    if (access != BufferAccess::ShaderWrite && access != BufferAccess::TransferDst && access != BufferAccess::HostWrite) {
        throw GraphError("pass '" + m_pass.name + "': write() was given a read access; use read()");
    }
    m_pass.buffers.push_back({buffer.id, stateOf(access), usageOf(access), true, access == BufferAccess::HostWrite});
}

void PassBuilder::sideEffect() { m_pass.sideEffect = true; }

// ---------------------------------------------------------------------------------------------
// PassContext
// ---------------------------------------------------------------------------------------------

rhi::TextureHandle PassContext::texture(GraphTexture texture) const {
    const PassNode &pass = m_graph.passes[m_pass];
    const bool declared = texture.valid() && std::any_of(pass.textures.begin(), pass.textures.end(),
                                                         [&](const TextureUse &u) { return u.id == texture.id; });
    if (!declared) {
        assert(false && "pass used a texture it did not declare");
        throw std::logic_error("pass '" + pass.name + "' used a texture it did not declare");
    }
    return m_graph.textures[texture.id].handle;
}

rhi::BufferHandle PassContext::buffer(GraphBuffer buffer) const {
    const PassNode &pass = m_graph.passes[m_pass];
    const bool declared = buffer.valid() && std::any_of(pass.buffers.begin(), pass.buffers.end(),
                                                        [&](const BufferUse &u) { return u.id == buffer.id; });
    if (!declared) {
        assert(false && "pass used a buffer it did not declare");
        throw std::logic_error("pass '" + pass.name + "' used a buffer it did not declare");
    }
    return m_graph.buffers[buffer.id].handle;
}

// ---------------------------------------------------------------------------------------------
// RenderGraph
// ---------------------------------------------------------------------------------------------

struct RenderGraph::Impl {
    struct TextureEntry {
        rhi::TextureDesc desc;
        std::string name;
        rhi::TextureHandle handle;
        uint64_t lastUsed = 0;
        bool inUse = false;
    };
    struct BufferEntry {
        rhi::BufferDesc desc;
        std::string name;
        rhi::BufferHandle handle;
        uint64_t lastUsed = 0;
        bool inUse = false;
    };

    explicit Impl(rhi::Device &d) : device(d) {}

    void validate();
    void cull();
    void acquireResources();
    void evictStaleEntries();

    rhi::Device &device;
    GraphData data;
    GraphBuilder builder;
    std::vector<std::unique_ptr<TextureEntry>> texturePool;
    std::vector<std::unique_ptr<BufferEntry>> bufferPool;
    uint64_t frame = 0;
};

RenderGraph::RenderGraph(rhi::Device &device) : m_impl(std::make_unique<Impl>(device)) {
    m_impl->builder.m_device = &device;
    m_impl->builder.m_graph = &m_impl->data;
}

RenderGraph::~RenderGraph() {
    for (const auto &entry : m_impl->texturePool) {
        m_impl->device.destroy(entry->handle);
    }
    for (const auto &entry : m_impl->bufferPool) {
        m_impl->device.destroy(entry->handle);
    }
}

GraphBuilder &RenderGraph::begin() {
    m_impl->data.clear();
    return m_impl->builder;
}

void RenderGraph::Impl::validate() {
    std::vector<char> textureWritten(data.textures.size(), 0);
    std::vector<char> bufferWritten(data.buffers.size(), 0);

    for (const PassNode &pass : data.passes) {
        // Reads must see an earlier write, unless the resource comes from outside the graph.
        for (const TextureUse &use : pass.textures) {
            const TextureNode &node = data.textures[use.id];
            if (!use.write && !node.imported && !textureWritten[use.id]) {
                throw GraphError("pass '" + pass.name + "' reads texture " + label(node.name, "texture", use.id) +
                                 " that no earlier pass writes");
            }
            for (const TextureUse &other : pass.textures) {
                if (other.id == use.id && other.write != use.write) {
                    throw GraphError("pass '" + pass.name + "' both reads and writes texture " +
                                     label(node.name, "texture", use.id) + "; use two passes");
                }
            }
        }
        for (const BufferUse &use : pass.buffers) {
            const BufferNode &node = data.buffers[use.id];
            if (!use.write && !node.imported && !bufferWritten[use.id]) {
                throw GraphError("pass '" + pass.name + "' reads buffer " + label(node.name, "buffer", use.id) +
                                 " that no earlier pass writes");
            }
            for (const BufferUse &other : pass.buffers) {
                if (other.id == use.id && other.write != use.write) {
                    throw GraphError("pass '" + pass.name + "' both reads and writes buffer " +
                                     label(node.name, "buffer", use.id) + "; use two passes");
                }
            }
        }

        // A target that keeps its old contents needs an earlier writer too.
        const auto checkLoad = [&](uint32_t id, bool clear) {
            const TextureNode &node = data.textures[id];
            if (!clear && !node.imported && !textureWritten[id]) {
                throw GraphError("pass '" + pass.name + "' loads texture " + label(node.name, "texture", id) +
                                 " that no earlier pass writes");
            }
        };
        for (const ColorDecl &color : pass.colors) {
            checkLoad(color.id, color.clear);
        }
        if (pass.hasDepth) {
            checkLoad(pass.depthId, pass.depthClear);
        }

        // All attachments of one pass share a size.
        bool haveExtent = false;
        uint32_t width = 0, height = 0;
        const auto checkExtent = [&](uint32_t id) {
            const rhi::TextureDesc &desc = data.textures[id].desc;
            if (!haveExtent) {
                haveExtent = true;
                width = desc.width;
                height = desc.height;
            } else if (desc.width != width || desc.height != height) {
                throw GraphError("pass '" + pass.name + "' has render targets of different sizes");
            }
        };
        for (const ColorDecl &color : pass.colors) {
            checkExtent(color.id);
        }
        if (pass.hasDepth) {
            checkExtent(pass.depthId);
        }

        for (const TextureUse &use : pass.textures) {
            textureWritten[use.id] |= use.write ? 1 : 0;
        }
        for (const BufferUse &use : pass.buffers) {
            bufferWritten[use.id] |= use.write ? 1 : 0;
        }
    }
}

void RenderGraph::Impl::cull() {
    std::vector<char> textureNeeded(data.textures.size(), 0);
    std::vector<char> bufferNeeded(data.buffers.size(), 0);

    for (size_t i = data.passes.size(); i-- > 0;) {
        PassNode &pass = data.passes[i];
        bool live = pass.sideEffect;
        for (const TextureUse &use : pass.textures) {
            live = live || (use.write && (data.textures[use.id].imported || textureNeeded[use.id]));
        }
        for (const BufferUse &use : pass.buffers) {
            live = live || (use.write && (data.buffers[use.id].imported || bufferNeeded[use.id]));
        }
        pass.live = live;
        if (!live) {
            continue;
        }
        for (const TextureUse &use : pass.textures) {
            textureNeeded[use.id] |= use.write ? 0 : 1;
        }
        for (const BufferUse &use : pass.buffers) {
            bufferNeeded[use.id] |= use.write ? 0 : 1;
        }
        for (const ColorDecl &color : pass.colors) {
            textureNeeded[color.id] |= color.clear ? 0 : 1; // a load needs what was there before
        }
        if (pass.hasDepth) {
            textureNeeded[pass.depthId] |= pass.depthClear ? 0 : 1;
        }
    }
}

void RenderGraph::Impl::acquireResources() {
    for (const auto &entry : texturePool) {
        entry->inUse = false;
    }
    for (const auto &entry : bufferPool) {
        entry->inUse = false;
    }

    // Usage = every access of every live pass, OR-ed together.
    std::vector<rhi::TextureUsage> textureUsage(data.textures.size());
    std::vector<rhi::BufferUsage> bufferUsage(data.buffers.size());
    std::vector<char> textureUsed(data.textures.size(), 0), bufferUsed(data.buffers.size(), 0);
    for (const PassNode &pass : data.passes) {
        if (!pass.live) {
            continue;
        }
        for (const TextureUse &use : pass.textures) {
            textureUsage[use.id] |= use.usage;
            textureUsed[use.id] = 1;
        }
        for (const BufferUse &use : pass.buffers) {
            bufferUsage[use.id] |= use.usage;
            bufferUsed[use.id] = 1;
        }
    }

    for (size_t i = 0; i < data.textures.size(); ++i) {
        TextureNode &node = data.textures[i];
        if (node.imported || !textureUsed[i]) {
            continue;
        }
        rhi::TextureDesc key = node.desc;
        key.usage = textureUsage[i];
        key.debugName = nullptr;
        const auto match = std::find_if(texturePool.begin(), texturePool.end(), [&](const auto &e) {
            return !e->inUse && e->desc.width == key.width && e->desc.height == key.height && e->desc.mips == key.mips &&
                   e->desc.format == key.format && e->desc.usage == key.usage;
        });
        if (match != texturePool.end()) {
            (*match)->inUse = true;
            (*match)->lastUsed = frame;
            node.handle = (*match)->handle;
            continue;
        }
        auto entry = std::make_unique<TextureEntry>();
        entry->desc = key;
        entry->name = node.name;
        rhi::TextureDesc request = key;
        request.debugName = entry->name.empty() ? nullptr : entry->name.c_str();
        entry->handle = device.createTexture(request);
        entry->inUse = true;
        entry->lastUsed = frame;
        node.handle = entry->handle;
        texturePool.push_back(std::move(entry));
    }

    for (size_t i = 0; i < data.buffers.size(); ++i) {
        BufferNode &node = data.buffers[i];
        if (node.imported || !bufferUsed[i]) {
            continue;
        }
        rhi::BufferDesc key = node.desc;
        key.usage = bufferUsage[i];
        if (key.usage == rhi::BufferUsage{}) {
            key.usage = rhi::BufferUsage::TransferDst; // only written by the host and never read: any usage will do
        }
        key.debugName = nullptr;
        const auto match = std::find_if(bufferPool.begin(), bufferPool.end(), [&](const auto &e) {
            return !e->inUse && e->desc.size == key.size && e->desc.usage == key.usage && e->desc.memory == key.memory;
        });
        if (match != bufferPool.end()) {
            (*match)->inUse = true;
            (*match)->lastUsed = frame;
            node.handle = (*match)->handle;
            continue;
        }
        auto entry = std::make_unique<BufferEntry>();
        entry->desc = key;
        entry->name = node.name;
        rhi::BufferDesc request = key;
        request.debugName = entry->name.empty() ? nullptr : entry->name.c_str();
        entry->handle = device.createBuffer(request);
        entry->inUse = true;
        entry->lastUsed = frame;
        node.handle = entry->handle;
        bufferPool.push_back(std::move(entry));
    }
}

void RenderGraph::Impl::evictStaleEntries() {
    const auto stale = [&](const auto &entry) { return !entry->inUse && frame - entry->lastUsed >= kPoolRetireFrames; };
    for (const auto &entry : texturePool) {
        if (stale(entry)) {
            device.destroy(entry->handle);
        }
    }
    for (const auto &entry : bufferPool) {
        if (stale(entry)) {
            device.destroy(entry->handle);
        }
    }
    std::erase_if(texturePool, stale);
    std::erase_if(bufferPool, stale);
}

void RenderGraph::compile() {
    Impl &impl = *m_impl;
    GraphData &graph = impl.data;
    graph.compiled = false;

    // 1. Capabilities
    const rhi::Capabilities &caps = impl.device.capabilities();
    std::vector<rhi::Feature> missing;
    for (const rhi::Feature feature : graph.required) {
        if (!caps.has(feature)) {
            missing.push_back(feature);
        }
    }
    if (!missing.empty()) {
        std::string message = "render graph requires features that device '" + caps.deviceName + "' does not support:";
        for (const rhi::Feature feature : missing) {
            message += " " + std::string(rhi::toString(feature));
        }
        throw GraphError(message, std::move(missing));
    }

    // 2. Validation, 3. Culling
    impl.validate();
    impl.cull();

    // 4. Usage derivation and transient resources
    ++impl.frame;
    impl.acquireResources();

    // 5. Barriers
    planBarriers(graph);

    impl.evictStaleEntries();
    graph.compiled = true;
}

rhi::SubmitTicket RenderGraph::execute() {
    Impl &impl = *m_impl;
    GraphData &graph = impl.data;
    if (!graph.compiled) {
        throw std::logic_error("RenderGraph::execute() called before a successful compile()");
    }

    rhi::CommandList &cmd = impl.device.beginCommands();
    for (size_t i = 0; i < graph.passes.size(); ++i) {
        PassNode &pass = graph.passes[i];
        if (!pass.live) {
            continue;
        }
        if (!pass.textureBarriers.empty() || !pass.bufferBarriers.empty()) {
            cmd.barrier(pass.textureBarriers, pass.bufferBarriers);
        }
        cmd.beginDebugLabel(pass.name.c_str());

        const bool rendering = !pass.colors.empty() || pass.hasDepth;
        if (rendering) {
            rhi::RenderingInfo info;
            for (const ColorDecl &color : pass.colors) {
                info.colors.push_back({graph.textures[color.id].handle, color.clear, color.value});
            }
            if (pass.hasDepth) {
                info.depth = rhi::DepthTarget{graph.textures[pass.depthId].handle, pass.depthClear, pass.depthValue};
            }
            const uint32_t sizeId = !pass.colors.empty() ? pass.colors.front().id : pass.depthId;
            info.width = graph.textures[sizeId].desc.width;
            info.height = graph.textures[sizeId].desc.height;
            cmd.beginRendering(info);
        }
        if (pass.execute) {
            PassContext context(cmd, impl.device, graph, i);
            pass.execute(context);
        }
        if (rendering) {
            cmd.endRendering();
        }
        cmd.endDebugLabel();
    }
    if (!graph.finalTextureBarriers.empty() || !graph.finalBufferBarriers.empty()) {
        cmd.barrier(graph.finalTextureBarriers, graph.finalBufferBarriers);
    }
    return impl.device.submit(cmd);
}

rhi::BufferHandle RenderGraph::resolve(GraphBuffer buffer) const {
    const GraphData &graph = m_impl->data;
    if (!buffer.valid() || buffer.id >= graph.buffers.size()) {
        throw GraphError("resolve: invalid buffer handle");
    }
    return graph.buffers[buffer.id].handle;
}

rhi::TextureHandle RenderGraph::resolve(GraphTexture texture) const {
    const GraphData &graph = m_impl->data;
    if (!texture.valid() || texture.id >= graph.textures.size()) {
        throw GraphError("resolve: invalid texture handle");
    }
    return graph.textures[texture.id].handle;
}

std::string RenderGraph::dump() const {
    const GraphData &graph = m_impl->data;
    std::ostringstream out;

    const auto writeBarriers = [&](const std::vector<rhi::TextureBarrier> &textures, const std::vector<rhi::BufferBarrier> &buffers) {
        for (const rhi::TextureBarrier &b : textures) {
            size_t index = 0;
            for (; index < graph.textures.size(); ++index) {
                if (graph.textures[index].handle == b.texture) {
                    break;
                }
            }
            out << "  barrier texture#" << index;
            if (index < graph.textures.size() && !graph.textures[index].name.empty()) {
                out << " '" << graph.textures[index].name << "'";
            }
            out << " " << stateName(b.from) << " -> " << stateName(b.to) << "\n";
        }
        for (const rhi::BufferBarrier &b : buffers) {
            size_t index = 0;
            for (; index < graph.buffers.size(); ++index) {
                if (graph.buffers[index].handle == b.buffer) {
                    break;
                }
            }
            out << "  barrier buffer#" << index;
            if (index < graph.buffers.size() && !graph.buffers[index].name.empty()) {
                out << " '" << graph.buffers[index].name << "'";
            }
            out << " " << stateName(b.from) << " -> " << stateName(b.to) << "\n";
        }
    };

    for (size_t i = 0; i < graph.passes.size(); ++i) {
        const PassNode &pass = graph.passes[i];
        out << "pass " << i << " \"" << pass.name << "\" " << (!graph.compiled ? "pending" : pass.live ? "live" : "culled") << "\n";
        if (graph.compiled && pass.live) {
            writeBarriers(pass.textureBarriers, pass.bufferBarriers);
        }
    }
    if (graph.compiled) {
        out << "final\n";
        writeBarriers(graph.finalTextureBarriers, graph.finalBufferBarriers);
    }
    return out.str();
}

} // namespace phox::graph
