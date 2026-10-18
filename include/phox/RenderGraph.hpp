#ifndef PHOX_RENDER_GRAPH_HPP
#define PHOX_RENDER_GRAPH_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace phox {

enum class ImageFormat { RGBA8 };

/// How a pass touches an image. The backend maps each usage to a layout/access/stage; the graph only
/// needs to know when the usage changes between passes.
enum class ImageUsage { Undefined, ColorAttachment, TransferSrc, TransferDst, Present };

constexpr std::uint32_t usageBit(ImageUsage usage) { return 1u << static_cast<std::uint32_t>(usage); }

struct ImageDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ImageFormat format = ImageFormat::RGBA8;
};

struct ResourceHandle {
    std::uint32_t id = UINT32_MAX;
    bool valid() const { return id != UINT32_MAX; }
    friend bool operator==(ResourceHandle a, ResourceHandle b) { return a.id == b.id; }
};

/// Everything a backend needs to know about an image resource.
struct ImageInfo {
    ResourceHandle handle;
    std::string name;
    ImageDesc desc;
    bool imported = false;          ///< owned by the caller (e.g. a swapchain image), not the graph
    std::uint32_t usageMask = 0;    ///< union of usageBit() over every usage in the compiled graph
};

/// A state change the graph inserts before a pass. `from == to` is a pure execution/memory barrier
/// (write-after-write on the same usage).
struct Transition {
    ResourceHandle resource;
    ImageUsage from = ImageUsage::Undefined;
    ImageUsage to = ImageUsage::Undefined;
};

/// Base for the backend-specific object handed to a pass's execute callback (for Vulkan it carries the
/// command buffer). Callbacks downcast it to their backend's type.
class PassContext {
public:
    virtual ~PassContext() = default;
};

/// What the graph drives while executing. `release` means "no later pass records a use of this image";
/// the backend decides when the memory is actually safe to free (commands may still be in flight).
class GraphBackend {
public:
    virtual ~GraphBackend() = default;
    virtual void allocate(const ImageInfo &image) = 0;
    virtual void release(const ImageInfo &image) = 0;
    virtual void transition(const ImageInfo &image, ImageUsage from, ImageUsage to) = 0;
    virtual PassContext &passContext() = 0;
};

class RenderGraph;

/// Handed to a pass's setup callback to declare what it reads and writes.
class PassBuilder {
public:
    /// The pass reads @p image in the given usage. The image must be written by an earlier pass (or be
    /// an imported image with a defined initial usage).
    void read(ResourceHandle image, ImageUsage usage);
    /// The pass writes @p image in the given usage.
    void write(ResourceHandle image, ImageUsage usage);
    /// Keep this pass even if nothing it writes is consumed (it has an effect outside the graph, e.g. a
    /// CPU readback). Writing to an imported image counts as an effect already.
    void sideEffects();

private:
    friend class RenderGraph;
    struct Access {
        ResourceHandle resource;
        ImageUsage usage;
        bool write;
    };
    explicit PassBuilder(RenderGraph &graph) : m_graph(graph) {}

    RenderGraph &m_graph;
    std::vector<Access> m_accesses;
    bool m_sideEffects = false;
};

/// Passes run in the order they are added. Compiling culls passes whose results nobody uses, derives
/// the layout transitions/barriers between the remaining passes, and works out when transient images
/// are first and last used.
class RenderGraph {
public:
    using SetupFn = std::function<void(PassBuilder &)>;
    using ExecuteFn = std::function<void(PassContext &)>;

    /// A graph-owned image that lives only for the passes that use it.
    ResourceHandle createImage(std::string name, ImageDesc desc);
    /// An externally owned image. @p initial is the usage it is in when the graph starts (Undefined means
    /// contents are discarded); the graph leaves it in @p final.
    ResourceHandle importImage(std::string name, ImageDesc desc, ImageUsage initial, ImageUsage final);

    /// @p setup runs immediately and declares the pass's accesses; @p execute runs during execute().
    /// @throws std::out_of_range if setup names an unknown resource
    void addPass(std::string name, const SetupFn &setup, ExecuteFn execute);

    /// @throws std::logic_error if the graph is invalid (a read with no producer, or one pass using one
    /// image in two different usages)
    void compile();

    /// Runs the compiled graph against @p backend. Compiles first if needed.
    void execute(GraphBackend &backend);

    // Introspection (after compile) - mainly for tests and debugging.
    struct CompiledPass {
        std::string name;
        bool culled = false;
        std::vector<Transition> transitions;  ///< applied immediately before the pass
    };
    const std::vector<CompiledPass> &compiledPasses() const { return m_compiled; }
    const std::vector<Transition> &finalTransitions() const { return m_finalTransitions; }
    const ImageInfo &image(ResourceHandle handle) const { return m_images.at(handle.id).info; }

private:
    friend class PassBuilder;

    struct ImageEntry {
        ImageInfo info;
        ImageUsage initial = ImageUsage::Undefined;
        ImageUsage final = ImageUsage::Undefined;
        int firstUse = -1;   // pass index, -1 if never used by a live pass
        int lastUse = -1;
    };
    struct Pass {
        std::string name;
        std::vector<PassBuilder::Access> accesses;
        bool sideEffects = false;
        ExecuteFn execute;
    };

    std::vector<ImageEntry> m_images;
    std::vector<Pass> m_passes;
    std::vector<CompiledPass> m_compiled;
    std::vector<Transition> m_finalTransitions;
    bool m_isCompiled = false;
};

} // namespace phox

#endif // PHOX_RENDER_GRAPH_HPP
