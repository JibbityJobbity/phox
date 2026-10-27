#include "phox/RenderGraph.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace phox {

namespace {

const char *usageName(ImageUsage usage) {
    switch (usage) {
    case ImageUsage::Undefined: return "Undefined";
    case ImageUsage::ColorAttachment: return "ColorAttachment";
    case ImageUsage::DepthAttachment: return "DepthAttachment";
    case ImageUsage::DepthReadOnly: return "DepthReadOnly";
    case ImageUsage::Sampled: return "Sampled";
    case ImageUsage::StorageRead: return "StorageRead";
    case ImageUsage::StorageWrite: return "StorageWrite";
    case ImageUsage::TransferSrc: return "TransferSrc";
    case ImageUsage::TransferDst: return "TransferDst";
    case ImageUsage::Present: return "Present";
    }
    return "?";
}

const char *usageName(BufferUsage usage) {
    switch (usage) {
    case BufferUsage::Undefined: return "Undefined";
    case BufferUsage::Vertex: return "Vertex";
    case BufferUsage::Index: return "Index";
    case BufferUsage::Uniform: return "Uniform";
    case BufferUsage::StorageRead: return "StorageRead";
    case BufferUsage::StorageWrite: return "StorageWrite";
    case BufferUsage::Indirect: return "Indirect";
    case BufferUsage::TransferSrc: return "TransferSrc";
    case BufferUsage::TransferDst: return "TransferDst";
    case BufferUsage::HostRead: return "HostRead";
    case BufferUsage::HostWrite: return "HostWrite";
    }
    return "?";
}

const char *passTypeName(PassType type) {
    switch (type) {
    case PassType::Graphics: return "graphics";
    case PassType::Compute: return "compute";
    case PassType::Transfer: return "transfer";
    }
    return "?";
}

bool readable(ImageUsage u) {
    return u == ImageUsage::DepthReadOnly || u == ImageUsage::Sampled || u == ImageUsage::StorageRead ||
           u == ImageUsage::TransferSrc;
}
bool writable(ImageUsage u) { return u == ImageUsage::StorageWrite || u == ImageUsage::TransferDst; }
bool readable(BufferUsage u) {
    return u == BufferUsage::Vertex || u == BufferUsage::Index || u == BufferUsage::Uniform ||
           u == BufferUsage::StorageRead || u == BufferUsage::Indirect || u == BufferUsage::TransferSrc;
}
bool writable(BufferUsage u) { return u == BufferUsage::StorageWrite || u == BufferUsage::TransferDst; }

std::string dotEscape(const std::string &text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

} // namespace

// ---- PassBuilder ------------------------------------------------------------------------------------

PassBuilder::PassBuilder(RenderGraph &graph, std::string_view passName, PassType type)
    : m_graph(graph), m_passName(passName), m_type(type) {}

std::string PassBuilder::describe(bool image, std::uint32_t index) const {
    return std::string(image ? "image '" : "buffer '") + m_graph.resource(image, index).name + "'";
}

PassBuilder::Access &PassBuilder::touch(bool image, std::uint32_t index, std::uint32_t version, std::uint8_t usage,
                                        bool strictRead, bool contentRead, bool writes) {
    const RenderGraph::Resource &res = m_graph.resource(image, index); // throws std::out_of_range
    const std::string pass = "pass '" + std::string(m_passName) + "'";
    if (version != res.version) {
        throw std::logic_error(pass + " uses version " + std::to_string(version) + " of " + describe(image, index) +
                               ", but it has been written since (version " + std::to_string(res.version) +
                               " is current)");
    }
    const bool contents = res.hasContents(version);
    if (strictRead && !contents) {
        throw std::logic_error(pass + " reads " + describe(image, index) + " before anything writes it");
    }
    for (Access &existing : m_accesses) {
        if (existing.image == image && existing.index == index) {
            if (existing.usage == usage && !existing.writes && !writes) {
                return existing; // the same read declared twice
            }
            throw std::logic_error(pass + " uses " + describe(image, index) +
                                   " more than once; a pass may use each resource in one way only");
        }
    }
    Access access;
    access.image = image;
    access.index = index;
    access.usage = usage;
    access.reads = (strictRead || contentRead) && contents;
    access.writes = writes;
    access.consumed = version;
    access.produced = writes ? version + 1 : version;
    m_accesses.push_back(access);
    return m_accesses.back();
}

ImageHandle PassBuilder::read(ImageHandle image, ImageUsage usage) {
    if (!readable(usage)) {
        throw std::logic_error("pass '" + std::string(m_passName) + "' cannot read an image as " + usageName(usage));
    }
    touch(true, image.index, image.version, static_cast<std::uint8_t>(usage), true, false, false);
    return image;
}

ImageHandle PassBuilder::write(ImageHandle image, ImageUsage usage) {
    if (usage == ImageUsage::ColorAttachment || usage == ImageUsage::DepthAttachment) {
        throw std::logic_error("pass '" + std::string(m_passName) +
                               "': render targets are declared with colorAttachment()/depthAttachment()");
    }
    if (!writable(usage)) {
        throw std::logic_error("pass '" + std::string(m_passName) + "' cannot write an image as " + usageName(usage));
    }
    // Storage and transfer writes may cover only part of the image, so they keep the old contents.
    const Access &access = touch(true, image.index, image.version, static_cast<std::uint8_t>(usage), false, true, true);
    return {image.index, access.produced};
}

BufferHandle PassBuilder::read(BufferHandle buffer, BufferUsage usage) {
    if (!readable(usage)) {
        throw std::logic_error("pass '" + std::string(m_passName) + "' cannot read a buffer as " + usageName(usage));
    }
    touch(false, buffer.index, buffer.version, static_cast<std::uint8_t>(usage), true, false, false);
    return buffer;
}

BufferHandle PassBuilder::write(BufferHandle buffer, BufferUsage usage) {
    if (!writable(usage)) {
        throw std::logic_error("pass '" + std::string(m_passName) + "' cannot write a buffer as " + usageName(usage));
    }
    const Access &access =
        touch(false, buffer.index, buffer.version, static_cast<std::uint8_t>(usage), false, true, true);
    return {buffer.index, access.produced};
}

ImageHandle PassBuilder::colorAttachment(ImageHandle image, LoadOp load, StoreOp store,
                                         std::array<float, 4> clearColor) {
    const std::string pass = "pass '" + std::string(m_passName) + "'";
    if (m_type != PassType::Graphics) {
        throw std::logic_error(pass + " is not a graphics pass, so it cannot have attachments");
    }
    m_graph.resource(true, image.index);
    if (isDepthFormat(m_graph.m_images[image.index].info.desc.format)) {
        throw std::logic_error(pass + " binds depth " + describe(true, image.index) + " as a colour attachment");
    }
    const Access &access = touch(true, image.index, image.version, static_cast<std::uint8_t>(ImageUsage::ColorAttachment),
                                 load == LoadOp::Load, false, true);
    Attachment attachment;
    attachment.image = image.index;
    attachment.load = load;
    attachment.store = store;
    attachment.clearColor = clearColor;
    m_colors.push_back(attachment);
    return {image.index, access.produced};
}

ImageHandle PassBuilder::depthAttachment(ImageHandle image, LoadOp load, StoreOp store, float clearDepth) {
    const std::string pass = "pass '" + std::string(m_passName) + "'";
    if (m_type != PassType::Graphics) {
        throw std::logic_error(pass + " is not a graphics pass, so it cannot have attachments");
    }
    m_graph.resource(true, image.index);
    if (!isDepthFormat(m_graph.m_images[image.index].info.desc.format)) {
        throw std::logic_error(pass + " binds " + describe(true, image.index) + ", which has no depth format, as depth");
    }
    if (m_depth) {
        throw std::logic_error(pass + " already has a depth attachment");
    }
    const Access &access = touch(true, image.index, image.version, static_cast<std::uint8_t>(ImageUsage::DepthAttachment),
                                 load == LoadOp::Load, false, true);
    Attachment attachment;
    attachment.image = image.index;
    attachment.load = load;
    attachment.store = store;
    attachment.clearDepth = clearDepth;
    m_depth = attachment;
    return {image.index, access.produced};
}

void PassBuilder::depthReadOnly(ImageHandle image) {
    const std::string pass = "pass '" + std::string(m_passName) + "'";
    if (m_type != PassType::Graphics) {
        throw std::logic_error(pass + " is not a graphics pass, so it cannot have attachments");
    }
    m_graph.resource(true, image.index);
    if (!isDepthFormat(m_graph.m_images[image.index].info.desc.format)) {
        throw std::logic_error(pass + " binds " + describe(true, image.index) + ", which has no depth format, as depth");
    }
    if (m_depth) {
        throw std::logic_error(pass + " already has a depth attachment");
    }
    touch(true, image.index, image.version, static_cast<std::uint8_t>(ImageUsage::DepthReadOnly), true, false, false);
    Attachment attachment;
    attachment.image = image.index;
    attachment.load = LoadOp::Load;
    attachment.readOnly = true;
    m_depth = attachment;
}

void PassBuilder::keep() { m_keep = true; }

// ---- RenderGraph: declaration ----------------------------------------------------------------------

RenderGraph::Resource &RenderGraph::resource(bool image, std::uint32_t index) {
    if (image) {
        if (index >= m_images.size()) throw std::out_of_range("unknown render graph image");
        return m_images[index];
    }
    if (index >= m_buffers.size()) throw std::out_of_range("unknown render graph buffer");
    return m_buffers[index];
}

const RenderGraph::Resource &RenderGraph::resource(bool image, std::uint32_t index) const {
    return const_cast<RenderGraph *>(this)->resource(image, index);
}

ImageHandle RenderGraph::createImage(std::string name, ImageDesc desc) {
    m_isCompiled = false;
    ImageResource res;
    res.name = name;
    res.info.index = static_cast<std::uint32_t>(m_images.size());
    res.info.name = std::move(name);
    res.info.desc = desc;
    m_images.push_back(std::move(res));
    return {m_images.back().info.index, 0};
}

BufferHandle RenderGraph::createBuffer(std::string name, BufferDesc desc) {
    m_isCompiled = false;
    BufferResource res;
    res.name = name;
    res.info.index = static_cast<std::uint32_t>(m_buffers.size());
    res.info.name = std::move(name);
    res.info.desc = desc;
    m_buffers.push_back(std::move(res));
    return {m_buffers.back().info.index, 0};
}

ImageHandle RenderGraph::importImage(std::string name, ImageDesc desc, ImageUsage initial, ImageUsage final) {
    if (initial == ImageUsage::Present) {
        throw std::logic_error("import '" + name + "': Present is only valid as a final state");
    }
    const ImageHandle handle = createImage(std::move(name), desc);
    ImageResource &res = m_images.back();
    res.imported = res.info.imported = true;
    res.initial = static_cast<std::uint8_t>(initial);
    res.final = static_cast<std::uint8_t>(final);
    return handle;
}

BufferHandle RenderGraph::importBuffer(std::string name, BufferDesc desc, BufferUsage initial, BufferUsage final) {
    if (initial == BufferUsage::HostRead || final == BufferUsage::HostWrite) {
        throw std::logic_error("import '" + name + "': HostWrite is only an initial state and HostRead only a final one");
    }
    const BufferHandle handle = createBuffer(std::move(name), desc);
    BufferResource &res = m_buffers.back();
    res.imported = res.info.imported = true;
    res.initial = static_cast<std::uint8_t>(initial);
    res.final = static_cast<std::uint8_t>(final);
    return handle;
}

void RenderGraph::addPass(std::string name, PassType type, const SetupFn &setup, ExecuteFn execute) {
    addPassImpl(std::move(name), type, setup, std::move(execute), nullptr);
}

void RenderGraph::addPassImpl(std::string name, PassType type, const SetupFn &setup, ExecuteFn execute,
                              std::shared_ptr<void> data) {
    PassBuilder builder(*this, name, type);
    if (setup) {
        setup(builder);
    }
    if (type == PassType::Graphics && builder.m_colors.empty() && !builder.m_depth) {
        throw std::logic_error("graphics pass '" + name + "' declares no attachments");
    }

    // Everything checked out: commit the versions this pass produces.
    const int index = static_cast<int>(m_passes.size());
    for (const PassBuilder::Access &access : builder.m_accesses) {
        if (access.writes) {
            Resource &res = resource(access.image, access.index);
            res.version = access.produced;
            res.producers.push_back(index);
        }
    }
    Pass pass;
    pass.name = std::move(name);
    pass.type = type;
    pass.accesses = std::move(builder.m_accesses);
    pass.colors = std::move(builder.m_colors);
    pass.depth = builder.m_depth;
    pass.keep = builder.m_keep;
    pass.execute = std::move(execute);
    pass.data = std::move(data);
    m_passes.push_back(std::move(pass));
    m_isCompiled = false;
}

// ---- RenderGraph: compile ---------------------------------------------------------------------------

void RenderGraph::compile() {
    const std::size_t passCount = m_passes.size();

    // 1. Resolve sizes and clear previous results.
    for (ImageResource &img : m_images) {
        if (const auto *absolute = std::get_if<Extent2D>(&img.info.desc.extent)) {
            img.info.extent = *absolute;
        } else {
            const float scale = std::get<RelativeExtent>(img.info.desc.extent).scale;
            if (m_output.width == 0 || m_output.height == 0) {
                throw std::logic_error("image '" + img.name +
                                       "' is sized relative to the output, but the graph has no output extent");
            }
            img.info.extent = {std::max(1u, static_cast<std::uint32_t>(std::lround(m_output.width * scale))),
                               std::max(1u, static_cast<std::uint32_t>(std::lround(m_output.height * scale)))};
        }
        img.info.usage = 0;
        img.firstUse = img.lastUse = -1;
        img.first.reset();
    }
    for (BufferResource &buf : m_buffers) {
        buf.info.usage = 0;
        buf.firstUse = buf.lastUse = -1;
    }

    // 2. Cull. Roots are kept passes and passes that write the final version of an import; a live pass
    //    makes the producer of every version it reads live. Producers come before consumers, so one
    //    backwards walk is enough.
    std::vector<bool> live(passCount, false);
    for (std::size_t i = passCount; i-- > 0;) {
        const Pass &pass = m_passes[i];
        bool isLive = live[i] || pass.keep;
        for (const PassBuilder::Access &access : pass.accesses) {
            const Resource &res = resource(access.image, access.index);
            if (access.writes && res.imported && access.produced == res.version) {
                isLive = true;
            }
        }
        live[i] = isLive;
        if (!isLive) {
            continue;
        }
        for (const PassBuilder::Access &access : pass.accesses) {
            if (access.reads) {
                const int producer = resource(access.image, access.index).producers[access.consumed];
                if (producer >= 0) {
                    live[static_cast<std::size_t>(producer)] = true;
                }
            }
        }
    }

    // 3. Walk the live passes in order, tracking each resource's state to derive barriers, lifetimes and
    //    the usages each resource needs. A barrier is needed when the usage changes or either side writes.
    struct State {
        std::uint8_t usage = 0;
        std::optional<PassType> pass;
        bool written = false;
    };
    std::vector<State> imageState(m_images.size());
    std::vector<State> bufferState(m_buffers.size());
    for (std::size_t i = 0; i < m_images.size(); ++i) {
        imageState[i] = {m_images[i].initial, std::nullopt, m_images[i].hasContents(0)};
    }
    for (std::size_t i = 0; i < m_buffers.size(); ++i) {
        bufferState[i] = {m_buffers[i].initial, std::nullopt, m_buffers[i].hasContents(0)};
    }

    m_compiled.assign(passCount, CompiledPass{});
    for (std::size_t p = 0; p < passCount; ++p) {
        const Pass &pass = m_passes[p];
        CompiledPass &out = m_compiled[p];
        out.name = pass.name;
        out.type = pass.type;
        out.culled = !live[p];
        if (out.culled) {
            continue;
        }
        const int index = static_cast<int>(p);
        for (const PassBuilder::Access &access : pass.accesses) {
            State &state = access.image ? imageState[access.index] : bufferState[access.index];
            if (state.usage != access.usage || state.written || access.writes) {
                if (access.image) {
                    out.barriers.images.push_back({access.index, static_cast<ImageUsage>(state.usage),
                                                   static_cast<ImageUsage>(access.usage), state.pass, pass.type});
                } else {
                    out.barriers.buffers.push_back({access.index, static_cast<BufferUsage>(state.usage),
                                                    static_cast<BufferUsage>(access.usage), state.pass, pass.type});
                }
            }
            state = {access.usage, pass.type, access.writes};

            Resource &res = resource(access.image, access.index);
            if (res.firstUse < 0) {
                res.firstUse = index;
                if (!res.imported) {
                    (access.image ? out.acquireImages : out.acquireBuffers).push_back(access.index);
                }
            }
            res.lastUse = index;
            if (access.image) {
                ImageResource &img = m_images[access.index];
                img.info.usage |= usageBit(static_cast<ImageUsage>(access.usage));
                if (!img.first) {
                    img.first = FirstUse{static_cast<ImageUsage>(access.usage), pass.type};
                }
            } else {
                m_buffers[access.index].info.usage |= usageBit(static_cast<BufferUsage>(access.usage));
            }
        }

        out.colorAttachments = pass.colors;
        out.depthAttachment = pass.depth;
        std::optional<Extent2D> area;
        const auto checkArea = [&](const Attachment &attachment) {
            const Extent2D extent = m_images[attachment.image].info.extent;
            if (area && !(*area == extent)) {
                throw std::logic_error("pass '" + pass.name + "' has attachments of different sizes");
            }
            area = extent;
        };
        for (const Attachment &attachment : pass.colors) checkArea(attachment);
        if (pass.depth) checkArea(*pass.depth);
        out.renderArea = area.value_or(Extent2D{});
    }
    for (std::size_t i = 0; i < m_images.size(); ++i) {
        if (!m_images[i].imported && m_images[i].lastUse >= 0) {
            m_compiled[static_cast<std::size_t>(m_images[i].lastUse)].releaseImages.push_back(static_cast<std::uint32_t>(i));
        }
    }
    for (std::size_t i = 0; i < m_buffers.size(); ++i) {
        if (!m_buffers[i].imported && m_buffers[i].lastUse >= 0) {
            m_compiled[static_cast<std::size_t>(m_buffers[i].lastUse)].releaseBuffers.push_back(static_cast<std::uint32_t>(i));
        }
    }

    // 4. Imports leave in the state they asked for (Undefined: whatever state they are in).
    m_finalBarriers = {};
    for (std::size_t i = 0; i < m_images.size(); ++i) {
        const ImageResource &img = m_images[i];
        if (img.imported && img.final != 0 && imageState[i].usage != img.final) {
            m_finalBarriers.images.push_back({static_cast<std::uint32_t>(i), static_cast<ImageUsage>(imageState[i].usage),
                                              static_cast<ImageUsage>(img.final), imageState[i].pass, std::nullopt});
        }
    }
    for (std::size_t i = 0; i < m_buffers.size(); ++i) {
        const BufferResource &buf = m_buffers[i];
        if (buf.imported && buf.final != 0 && bufferState[i].usage != buf.final) {
            m_finalBarriers.buffers.push_back({static_cast<std::uint32_t>(i), static_cast<BufferUsage>(bufferState[i].usage),
                                               static_cast<BufferUsage>(buf.final), bufferState[i].pass, std::nullopt});
        }
    }

    m_isCompiled = true;
}

void RenderGraph::execute(GraphBackend &backend) {
    if (!m_isCompiled) {
        compile();
    }
    for (std::size_t p = 0; p < m_passes.size(); ++p) {
        const CompiledPass &pass = m_compiled[p];
        if (pass.culled) {
            continue;
        }
        for (std::uint32_t i : pass.acquireImages) backend.acquire(m_images[i].info);
        for (std::uint32_t i : pass.acquireBuffers) backend.acquire(m_buffers[i].info);
        if (!pass.barriers.empty()) {
            backend.barriers(pass.barriers);
        }
        backend.beginPass(pass);
        if (m_passes[p].execute) {
            m_passes[p].execute(backend.passContext());
        }
        backend.endPass(pass);
        for (std::uint32_t i : pass.releaseImages) backend.release(m_images[i].info);
        for (std::uint32_t i : pass.releaseBuffers) backend.release(m_buffers[i].info);
    }
    if (!m_finalBarriers.empty()) {
        backend.barriers(m_finalBarriers);
    }
}

// ---- RenderGraph: introspection ---------------------------------------------------------------------

void RenderGraph::requireCompiled() const {
    if (!m_isCompiled) {
        throw std::logic_error("the render graph has not been compiled");
    }
}

const std::vector<CompiledPass> &RenderGraph::compiledPasses() const {
    requireCompiled();
    return m_compiled;
}

const BarrierBatch &RenderGraph::finalBarriers() const {
    requireCompiled();
    return m_finalBarriers;
}

const ImageInfo &RenderGraph::imageInfo(std::uint32_t index) const {
    if (index >= m_images.size()) throw std::out_of_range("unknown render graph image");
    return m_images[index].info;
}

const BufferInfo &RenderGraph::bufferInfo(std::uint32_t index) const {
    if (index >= m_buffers.size()) throw std::out_of_range("unknown render graph buffer");
    return m_buffers[index].info;
}

std::optional<RenderGraph::FirstUse> RenderGraph::firstUse(ImageHandle image) const {
    requireCompiled();
    if (image.index >= m_images.size()) throw std::out_of_range("unknown render graph image");
    return m_images[image.index].first;
}

std::string RenderGraph::toDot() const {
    requireCompiled();
    std::ostringstream dot;
    dot << "digraph RenderGraph {\n"
        << "  rankdir=LR;\n"
        << "  node [fontname=\"sans-serif\", fontsize=10];\n"
        << "  edge [fontname=\"sans-serif\", fontsize=9];\n";

    const auto node = [](bool image, std::uint32_t index, std::uint32_t version) {
        return std::string(image ? "\"i" : "\"b") + std::to_string(index) + "v" + std::to_string(version) + "\"";
    };
    std::vector<std::string> resourceNodes;
    const auto declare = [&](bool image, std::uint32_t index, std::uint32_t version) {
        const Resource &res = resource(image, index);
        std::string decl = "  " + node(image, index, version) + " [shape=" + (image ? "ellipse" : "note") +
                           ", label=\"" + dotEscape(res.name) + " v" + std::to_string(version) + "\"";
        if (res.imported) decl += ", style=filled, fillcolor=\"#f4e3c1\"";
        decl += "];\n";
        if (std::find(resourceNodes.begin(), resourceNodes.end(), decl) == resourceNodes.end()) {
            resourceNodes.push_back(decl);
        }
    };

    std::ostringstream edges;
    for (std::size_t p = 0; p < m_passes.size(); ++p) {
        const Pass &pass = m_passes[p];
        const bool culled = m_compiled[p].culled;
        dot << "  \"p" << p << "\" [shape=box, label=\"" << dotEscape(pass.name) << "\\n" << passTypeName(pass.type)
            << (culled ? " (culled)" : "") << "\", "
            << (culled ? "style=\"rounded,dashed\", color=gray60, fontcolor=gray60"
                       : "style=\"rounded,filled\", fillcolor=\"#dbe8f7\"")
            << "];\n";
        const std::string style = culled ? ", color=gray60, fontcolor=gray60" : "";
        for (const PassBuilder::Access &access : pass.accesses) {
            const char *usage = access.image ? usageName(static_cast<ImageUsage>(access.usage))
                                             : usageName(static_cast<BufferUsage>(access.usage));
            if (access.reads || !access.writes) {
                declare(access.image, access.index, access.consumed);
                edges << "  " << node(access.image, access.index, access.consumed) << " -> \"p" << p
                      << "\" [label=\"" << usage << "\"" << style << "];\n";
            }
            if (access.writes) {
                declare(access.image, access.index, access.produced);
                edges << "  \"p" << p << "\" -> " << node(access.image, access.index, access.produced) << " [label=\""
                      << usage << "\"" << style << "];\n";
            }
        }
    }
    for (const std::string &decl : resourceNodes) dot << decl;
    dot << edges.str() << "}\n";
    return dot.str();
}

} // namespace phox
