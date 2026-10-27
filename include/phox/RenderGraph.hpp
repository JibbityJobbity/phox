#ifndef PHOX_RENDER_GRAPH_HPP
#define PHOX_RENDER_GRAPH_HPP

#include "phox/Resources.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

/// A frame graph: passes declare the images and buffers they use and how, and the graph works out
/// which passes matter, when transient resources live, and every barrier between passes.
///
/// Passes run in the order they are added. The graph is meant to be rebuilt every frame; it holds no
/// GPU objects itself (a GraphBackend creates and binds them).
namespace phox {

inline constexpr std::uint32_t kInvalidResource = UINT32_MAX;

/// One version of an image. Each write produces a new version; a pass can only use the current one,
/// which is how the graph tracks dependencies.
struct ImageHandle {
    std::uint32_t index = kInvalidResource;
    std::uint32_t version = 0;
    bool valid() const { return index != kInvalidResource; }
    friend bool operator==(const ImageHandle &, const ImageHandle &) = default;
};

/// One version of a buffer. See ImageHandle.
struct BufferHandle {
    std::uint32_t index = kInvalidResource;
    std::uint32_t version = 0;
    bool valid() const { return index != kInvalidResource; }
    friend bool operator==(const BufferHandle &, const BufferHandle &) = default;
};

/// A size given as a fraction of the graph's output extent, resolved when the graph compiles.
struct RelativeExtent {
    float scale = 1.0f;
};
using ExtentSpec = std::variant<Extent2D, RelativeExtent>;

struct ImageDesc {
    ExtentSpec extent = RelativeExtent{};
    Format format = Format::RGBA8Unorm;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
};

struct BufferDesc {
    std::uint64_t size = 0;
};

enum class PassType : std::uint8_t { Graphics, Compute, Transfer };
enum class LoadOp : std::uint8_t { Load, Clear, DontCare };
enum class StoreOp : std::uint8_t { Store, DontCare };

/// What a backend needs to create or bind an image.
struct ImageInfo {
    std::uint32_t index = kInvalidResource;
    std::string name;
    ImageDesc desc;
    Extent2D extent;          ///< desc.extent resolved against the output extent (set by compile())
    bool imported = false;    ///< owned outside the graph (e.g. a swapchain image)
    ImageUsageMask usage = 0; ///< every usage of the image in the compiled graph
};

/// What a backend needs to create or bind a buffer.
struct BufferInfo {
    std::uint32_t index = kInvalidResource;
    std::string name;
    BufferDesc desc;
    bool imported = false;
    BufferUsageMask usage = 0;
};

/// A state change the graph needs before a pass (or at the end of the graph). from == to is a pure
/// execution and memory dependency, e.g. between two passes that write the same attachment.
struct ImageBarrier {
    std::uint32_t image = 0;
    ImageUsage from = ImageUsage::Undefined;
    ImageUsage to = ImageUsage::Undefined;
    std::optional<PassType> fromPass; ///< empty: the state the image entered the graph in
    std::optional<PassType> toPass;   ///< empty: the state the image leaves the graph in
};

struct BufferBarrier {
    std::uint32_t buffer = 0;
    BufferUsage from = BufferUsage::Undefined;
    BufferUsage to = BufferUsage::Undefined;
    std::optional<PassType> fromPass;
    std::optional<PassType> toPass;
};

/// Barriers issued together, as one call in the backend.
struct BarrierBatch {
    std::vector<ImageBarrier> images;
    std::vector<BufferBarrier> buffers;
    bool empty() const { return images.empty() && buffers.empty(); }
};

/// An image bound as a render target for a graphics pass.
struct Attachment {
    std::uint32_t image = 0;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    std::array<float, 4> clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    float clearDepth = 1.0f;
    bool readOnly = false; ///< a depth attachment that is tested but not written
};

/// A pass as the backend sees it.
struct CompiledPass {
    std::string name;
    PassType type = PassType::Graphics;
    bool culled = false;
    BarrierBatch barriers;                    ///< issued before the pass
    std::vector<Attachment> colorAttachments; ///< graphics passes only
    std::optional<Attachment> depthAttachment;
    Extent2D renderArea;                      ///< the attachments' extent (graphics passes only)
    std::vector<std::uint32_t> acquireImages; ///< transients first used by this pass
    std::vector<std::uint32_t> releaseImages; ///< transients last used by this pass
    std::vector<std::uint32_t> acquireBuffers;
    std::vector<std::uint32_t> releaseBuffers;
};

/// Base for the backend-specific object handed to a pass's execute callback (for Vulkan it carries the
/// command buffer and the resources). Callbacks downcast it to their backend's type.
class PassContext {
public:
    virtual ~PassContext() = default;
};

/// What the graph drives while executing. For each live pass, in order: acquire the transients it
/// uses first, issue its barriers, beginPass, run the pass, endPass, release the transients it uses
/// last. Then the final barriers. "release" means no later pass records a use; the backend decides
/// when memory is actually reusable.
class GraphBackend {
public:
    virtual ~GraphBackend() = default;
    virtual void acquire(const ImageInfo &image) = 0;
    virtual void acquire(const BufferInfo &buffer) = 0;
    virtual void release(const ImageInfo &image) = 0;
    virtual void release(const BufferInfo &buffer) = 0;
    virtual void barriers(const BarrierBatch &batch) = 0;
    /// For graphics passes, begins rendering to the pass's attachments.
    virtual void beginPass(const CompiledPass &pass) = 0;
    virtual void endPass(const CompiledPass &pass) = 0;
    virtual PassContext &passContext() = 0;
};

class RenderGraph;

/// Handed to a pass's setup callback to declare what the pass uses. Every method checks the request
/// immediately and throws std::out_of_range for an unknown resource or std::logic_error for an invalid
/// use, leaving the graph unchanged.
class PassBuilder {
public:
    /// Reads the current version of @p image. It must have contents (a producer, or an import that
    /// arrives in a defined state). Allowed usages: DepthReadOnly, Sampled, StorageRead, TransferSrc.
    ImageHandle read(ImageHandle image, ImageUsage usage);
    /// Writes @p image and returns the new version. Allowed usages: StorageWrite, TransferDst (both may
    /// leave parts of the image untouched, so they keep the previous contents' producer alive).
    ImageHandle write(ImageHandle image, ImageUsage usage);
    /// Allowed usages: Vertex, Index, Uniform, StorageRead, Indirect, TransferSrc.
    BufferHandle read(BufferHandle buffer, BufferUsage usage);
    /// Allowed usages: StorageWrite, TransferDst.
    BufferHandle write(BufferHandle buffer, BufferUsage usage);

    /// Renders to @p image (graphics passes). LoadOp::Load needs existing contents; Clear and DontCare
    /// overwrite the whole image. Attachments are bound in the order declared.
    ImageHandle colorAttachment(ImageHandle image, LoadOp load, StoreOp store = StoreOp::Store,
                                std::array<float, 4> clearColor = {0.0f, 0.0f, 0.0f, 1.0f});
    /// Depth test and write (graphics passes).
    ImageHandle depthAttachment(ImageHandle image, LoadOp load, StoreOp store = StoreOp::Store,
                                float clearDepth = 1.0f);
    /// Depth test against @p image without writing it (graphics passes). The image keeps its version.
    void depthReadOnly(ImageHandle image);

    /// Never cull this pass, even if nothing it writes is used (e.g. it has effects outside the graph).
    /// Writing an imported resource already counts as such an effect.
    void keep();

private:
    friend class RenderGraph;

    struct Access {
        bool image = true; ///< false: a buffer
        std::uint32_t index = 0;
        std::uint8_t usage = 0;
        bool reads = false;  ///< depends on the contents of version `consumed`
        bool writes = false; ///< produces version `produced`
        std::uint32_t consumed = 0;
        std::uint32_t produced = 0;
    };

    PassBuilder(RenderGraph &graph, std::string_view passName, PassType type);
    Access &touch(bool image, std::uint32_t index, std::uint32_t version, std::uint8_t usage, bool strictRead,
                  bool contentRead, bool writes);
    std::string describe(bool image, std::uint32_t index) const;

    RenderGraph &m_graph;
    std::string_view m_passName;
    PassType m_type;
    std::vector<Access> m_accesses;
    std::vector<Attachment> m_colors;
    std::optional<Attachment> m_depth;
    bool m_keep = false;
};

class RenderGraph {
public:
    using SetupFn = std::function<void(PassBuilder &)>;
    using ExecuteFn = std::function<void(PassContext &)>;

    /// @param output  the extent RelativeExtent sizes are fractions of (usually the swapchain's)
    explicit RenderGraph(Extent2D output = {}) : m_output(output) {}

    Extent2D outputExtent() const { return m_output; }

    /// A graph-owned image that lives only for the passes that use it.
    ImageHandle createImage(std::string name, ImageDesc desc);
    /// A graph-owned buffer that lives only for the passes that use it.
    BufferHandle createBuffer(std::string name, BufferDesc desc);
    /// An image owned outside the graph. @p initial is its state when the graph starts (Undefined means
    /// the contents don't matter); the graph leaves it in @p final (Undefined: whatever state it is in).
    ImageHandle importImage(std::string name, ImageDesc desc, ImageUsage initial, ImageUsage final);
    /// A buffer owned outside the graph. HostWrite as @p initial means the CPU filled it before the
    /// frame; HostRead as @p final makes the GPU's writes visible to the CPU once the frame completes.
    BufferHandle importBuffer(std::string name, BufferDesc desc, BufferUsage initial, BufferUsage final);

    /// Adds a pass. @p setup runs immediately and declares the pass's resources; @p execute runs during
    /// execute(). Throws (see PassBuilder) if the declarations are invalid; the graph is then unchanged.
    void addPass(std::string name, PassType type, const SetupFn &setup, ExecuteFn execute);

    /// Adds a pass whose setup fills a Data struct (typically with the handles it produced) that its
    /// execute callback then receives. Returns the Data, so later passes can use those handles.
    ///   setup:   void(PassBuilder &, Data &)
    ///   execute: void(const Data &, PassContext &)
    template <class Data, class Setup, class Execute>
    const Data &addPass(std::string name, PassType type, Setup &&setup, Execute &&execute) {
        auto data = std::make_shared<Data>();
        Data &ref = *data;
        addPassImpl(
            std::move(name), type, [&](PassBuilder &builder) { setup(builder, ref); },
            [data, exec = std::forward<Execute>(execute)](PassContext &context) {
                exec(static_cast<const Data &>(*data), context);
            },
            data);
        return ref;
    }

    /// Culls passes nothing depends on, resolves sizes, and derives barriers and resource lifetimes.
    /// @throws std::logic_error if a RelativeExtent has no output extent to resolve against, or a pass's
    ///         attachments differ in size
    void compile();
    /// Runs the compiled graph against @p backend, compiling first if needed.
    void execute(GraphBackend &backend);

    // Introspection after compile(): for backends, tests and tools.
    struct FirstUse {
        ImageUsage usage;
        PassType pass;
    };
    bool compiled() const { return m_isCompiled; }
    const std::vector<CompiledPass> &compiledPasses() const;
    const BarrierBatch &finalBarriers() const;
    const ImageInfo &imageInfo(std::uint32_t index) const;
    const ImageInfo &imageInfo(ImageHandle handle) const { return imageInfo(handle.index); }
    const BufferInfo &bufferInfo(std::uint32_t index) const;
    const BufferInfo &bufferInfo(BufferHandle handle) const { return bufferInfo(handle.index); }
    std::size_t imageCount() const { return m_images.size(); }
    std::size_t bufferCount() const { return m_buffers.size(); }
    /// How the first live pass uses @p image; empty if no live pass does. For an image that arrives
    /// through a semaphore (a swapchain image), this is the stage that has to wait for it.
    std::optional<FirstUse> firstUse(ImageHandle image) const;
    /// The compiled graph in Graphviz DOT: passes, resource versions, and culled passes greyed out.
    std::string toDot() const;

private:
    friend class PassBuilder;

    struct Resource {
        std::string name;
        bool imported = false;
        std::uint8_t initial = 0; ///< usage value; Undefined means no meaningful contents
        std::uint8_t final = 0;
        std::uint32_t version = 0;
        std::vector<int> producers{-1}; ///< producers[v]: the pass that wrote version v (-1: none)
        int firstUse = -1;
        int lastUse = -1;
        bool hasContents(std::uint32_t v) const { return v > 0 || (imported && initial != 0); }
    };
    struct ImageResource : Resource {
        ImageInfo info;
        std::optional<FirstUse> first;
    };
    struct BufferResource : Resource {
        BufferInfo info;
    };
    struct Pass {
        std::string name;
        PassType type = PassType::Graphics;
        std::vector<PassBuilder::Access> accesses;
        std::vector<Attachment> colors;
        std::optional<Attachment> depth;
        bool keep = false;
        ExecuteFn execute;
        std::shared_ptr<void> data;
    };

    void addPassImpl(std::string name, PassType type, const SetupFn &setup, ExecuteFn execute,
                     std::shared_ptr<void> data);
    Resource &resource(bool image, std::uint32_t index);
    const Resource &resource(bool image, std::uint32_t index) const;
    void requireCompiled() const;

    Extent2D m_output;
    std::vector<ImageResource> m_images;
    std::vector<BufferResource> m_buffers;
    std::vector<Pass> m_passes;
    std::vector<CompiledPass> m_compiled;
    BarrierBatch m_finalBarriers;
    bool m_isCompiled = false;
};

} // namespace phox

#endif // PHOX_RENDER_GRAPH_HPP
