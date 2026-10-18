#include "phox/RenderGraph.hpp"

#include <stdexcept>

namespace phox {

void PassBuilder::read(ResourceHandle image, ImageUsage usage) {
    if (image.id >= m_graph.m_images.size()) {
        throw std::out_of_range("PassBuilder::read: unknown resource");
    }
    m_accesses.push_back({image, usage, false});
}

void PassBuilder::write(ResourceHandle image, ImageUsage usage) {
    if (image.id >= m_graph.m_images.size()) {
        throw std::out_of_range("PassBuilder::write: unknown resource");
    }
    m_accesses.push_back({image, usage, true});
}

void PassBuilder::sideEffects() { m_sideEffects = true; }

ResourceHandle RenderGraph::createImage(std::string name, ImageDesc desc) {
    m_isCompiled = false;
    ImageEntry entry;
    entry.info.handle = ResourceHandle{static_cast<std::uint32_t>(m_images.size())};
    entry.info.name = std::move(name);
    entry.info.desc = desc;
    m_images.push_back(std::move(entry));
    return m_images.back().info.handle;
}

ResourceHandle RenderGraph::importImage(std::string name, ImageDesc desc, ImageUsage initial, ImageUsage final) {
    const ResourceHandle handle = createImage(std::move(name), desc);
    ImageEntry &entry = m_images.back();
    entry.info.imported = true;
    entry.initial = initial;
    entry.final = final;
    return handle;
}

void RenderGraph::addPass(std::string name, const SetupFn &setup, ExecuteFn execute) {
    m_isCompiled = false;
    PassBuilder builder(*this);
    if (setup) {
        setup(builder);
    }
    Pass pass;
    pass.name = std::move(name);
    pass.accesses = std::move(builder.m_accesses);
    pass.sideEffects = builder.m_sideEffects;
    pass.execute = std::move(execute);
    m_passes.push_back(std::move(pass));
}

namespace {

// One pass's combined access to one image.
struct MergedAccess {
    ResourceHandle resource;
    ImageUsage usage;
    bool read = false;
    bool write = false;
};

} // namespace

void RenderGraph::compile() {
    const std::size_t nPasses = m_passes.size();
    const std::size_t nImages = m_images.size();

    // 1. Merge each pass's accesses per image. A pass can read and write the same image (e.g. blending)
    //    but only in a single usage.
    std::vector<std::vector<MergedAccess>> merged(nPasses);
    for (std::size_t p = 0; p < nPasses; ++p) {
        for (const auto &access : m_passes[p].accesses) {
            MergedAccess *existing = nullptr;
            for (auto &m : merged[p]) {
                if (m.resource == access.resource) {
                    existing = &m;
                    break;
                }
            }
            if (existing == nullptr) {
                merged[p].push_back({access.resource, access.usage, !access.write, access.write});
                continue;
            }
            if (existing->usage != access.usage) {
                throw std::logic_error("pass '" + m_passes[p].name + "' uses image '" +
                                       m_images[access.resource.id].info.name + "' in two different usages");
            }
            existing->read = existing->read || !access.write;
            existing->write = existing->write || access.write;
        }
    }

    // 2. Cull backwards from the passes that matter: those with side effects or that write an imported
    //    image, then anything that produces an image a live pass reads.
    std::vector<bool> live(nPasses, false);
    std::vector<bool> needed(nImages, false);
    for (std::size_t i = nPasses; i-- > 0;) {
        bool isLive = m_passes[i].sideEffects;
        for (const auto &m : merged[i]) {
            if (m.write && (m_images[m.resource.id].info.imported || needed[m.resource.id])) {
                isLive = true;
            }
        }
        live[i] = isLive;
        if (isLive) {
            for (const auto &m : merged[i]) {
                if (m.read) {
                    needed[m.resource.id] = true;
                }
            }
        }
    }

    // 3. Walk the live passes in order: check every read has a producer, derive transitions, and record
    //    first/last use and the set of usages per image.
    std::vector<bool> written(nImages, false);
    std::vector<ImageUsage> current(nImages, ImageUsage::Undefined);
    std::vector<bool> lastWasWrite(nImages, false);
    std::vector<std::uint32_t> usageMask(nImages, 0);
    for (std::size_t i = 0; i < nImages; ++i) {
        m_images[i].firstUse = m_images[i].lastUse = -1;
        if (m_images[i].info.imported) {
            current[i] = m_images[i].initial;
            written[i] = m_images[i].initial != ImageUsage::Undefined;
        }
    }

    m_compiled.assign(nPasses, CompiledPass{});
    for (std::size_t p = 0; p < nPasses; ++p) {
        m_compiled[p].name = m_passes[p].name;
        m_compiled[p].culled = !live[p];
        if (!live[p]) {
            continue;
        }
        for (const auto &m : merged[p]) {
            const std::uint32_t id = m.resource.id;
            if (m.read && !m.write && !written[id]) {
                throw std::logic_error("pass '" + m_passes[p].name + "' reads image '" + m_images[id].info.name +
                                       "' before any pass writes it");
            }
            // A layout change, or any hazard (previous or this access writes), needs a barrier.
            if (current[id] != m.usage || lastWasWrite[id] || m.write) {
                m_compiled[p].transitions.push_back({m.resource, current[id], m.usage});
            }
            current[id] = m.usage;
            lastWasWrite[id] = m.write;
            if (m.write) {
                written[id] = true;
            }
            usageMask[id] |= usageBit(m.usage);
            if (m_images[id].firstUse < 0) {
                m_images[id].firstUse = static_cast<int>(p);
            }
            m_images[id].lastUse = static_cast<int>(p);
        }
    }

    // 4. Imported images end in their requested final usage.
    m_finalTransitions.clear();
    for (std::size_t i = 0; i < nImages; ++i) {
        m_images[i].info.usageMask = usageMask[i];
        if (m_images[i].info.imported && m_images[i].firstUse >= 0 && current[i] != m_images[i].final) {
            m_finalTransitions.push_back({m_images[i].info.handle, current[i], m_images[i].final});
        }
    }

    m_isCompiled = true;
}

void RenderGraph::execute(GraphBackend &backend) {
    if (!m_isCompiled) {
        compile();
    }

    for (std::size_t p = 0; p < m_passes.size(); ++p) {
        if (m_compiled[p].culled) {
            continue;
        }
        const int index = static_cast<int>(p);
        for (const auto &entry : m_images) {
            if (!entry.info.imported && entry.firstUse == index) {
                backend.allocate(entry.info);
            }
        }
        for (const auto &t : m_compiled[p].transitions) {
            backend.transition(m_images[t.resource.id].info, t.from, t.to);
        }
        if (m_passes[p].execute) {
            m_passes[p].execute(backend.passContext());
        }
        for (const auto &entry : m_images) {
            if (!entry.info.imported && entry.lastUse == index) {
                backend.release(entry.info);
            }
        }
    }
    for (const auto &t : m_finalTransitions) {
        backend.transition(m_images[t.resource.id].info, t.from, t.to);
    }
}

} // namespace phox
