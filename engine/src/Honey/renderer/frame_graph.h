#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>

#include "framebuffer.h"
#include "Honey/core/base.h"

namespace Honey {

    typedef struct VkCommandBuffer_T* VkCommandBuffer;
    using FGVulkanRecordCommands = std::function<void(VkCommandBuffer)>;

    // Common handles and enums

    using FGResourceHandle = uint32_t;
    using FGPassHandle     = uint32_t;

    static constexpr FGResourceHandle k_invalid_resource = UINT32_MAX;
    static constexpr FGPassHandle     k_invalid_pass     = UINT32_MAX;
    static constexpr uint32_t         k_invalid_physical = UINT32_MAX;

    enum class FGResourceType : uint8_t {
        Texture,
        Buffer,
        ImportedTarget
    };

    enum class FGQueueDomain : uint8_t {
        Graphics,
        Compute,
        Transfer
    };

    enum class FGResourceUsage : uint8_t {
        Unknown,

        // Generic read-style usages
        Sampled,
        Uniform,
        Indirect,
        VertexBuffer,
        IndexBuffer,
        TransferSrc,

        // Generic write-style usages
        ColorAttachment,
        DepthAttachment,
        StorageWrite,
        TransferDst,

        // Read+write
        StorageRead,
        StorageReadWrite
    };

    enum class FGImportedTargetKind : uint8_t {
        Swapchain,
        ExternalFramebuffer
    };

    enum class FGSizeMode : uint8_t {
        Fixed,
        SwapchainRelative
    };

    enum class FGDiagSeverity : uint8_t {
        Info,
        Warning,
        Error
    };

    // -------------------------------------------------------------------------
    // Parsed graph descriptors (loaded from YAML)
    // -------------------------------------------------------------------------

    struct FGTextureDesc {
        FramebufferTextureFormat format = FramebufferTextureFormat::None;

        FGSizeMode size_mode = FGSizeMode::SwapchainRelative;

        // Used when size_mode == Fixed
        uint32_t width = 0;
        uint32_t height = 0;

        // Used when size_mode == SwapchainRelative
        float scale_x = 1.0f;
        float scale_y = 1.0f;

        uint32_t samples = 1;
    };

    struct FGBufferDesc {
        uint64_t size = 0;
        uint32_t usage_flags = 0;
    };

    struct FGResourceBindingDesc {
        std::string resource_name;
        FGResourceUsage usage = FGResourceUsage::Unknown;
    };

    struct FGResourceDesc {
        std::string name;
        FGResourceType type = FGResourceType::Texture;

        // Type == Texture
        FGTextureDesc texture{};

        // Type == Buffer
        FGBufferDesc buffer{};

        // Type == ImportedTarget
        FGImportedTargetKind imported_kind = FGImportedTargetKind::Swapchain;
    };

    struct FGPassDesc {
        std::string name;
        std::string executor_id;

        FGQueueDomain queue_domain = FGQueueDomain::Graphics;

        std::vector<std::string> reads;
        std::vector<std::string> writes;
        std::vector<FGResourceBindingDesc> read_bindings;
        std::vector<FGResourceBindingDesc> write_bindings;

        // Optional free-form YAML data available to runtime/executor code.
        YAML::Node clear_node;
        YAML::Node params_node;
    };

    struct FGGraphDesc {
        uint32_t version = 1;
        std::vector<FGResourceDesc> resources;
        std::vector<FGPassDesc> passes;
    };

    // -------------------------------------------------------------------------
    // Diagnostics
    // -------------------------------------------------------------------------

    struct FGDiagnostic {
        FGDiagSeverity severity = FGDiagSeverity::Error;
        std::string message;
        std::string file;
        std::string scope;
    };

    struct FGCompileDiagnostics {
        std::vector<FGDiagnostic> entries;

        bool has_errors() const;
        void add_error(std::string_view message, std::string_view scope = {});
        void add_warning(std::string_view message, std::string_view scope = {});
        void add_info(std::string_view message, std::string_view scope = {});
    };

    struct FGCompileOptions {
        // Resource-name -> externally owned framebuffer binding.
        // Used by ImportedTarget resources with kind ExternalFramebuffer.
        std::unordered_map<std::string, Ref<Framebuffer>> external_framebuffers;

        // Optional explicit list of output resources to keep alive.
        // If empty, compiler treats all ImportedTarget resources as outputs.
        std::vector<std::string> requested_output_resources;
    };

    struct FGPassExecutionStat {
        std::string pass_name;
        double cpu_time_ms = 0.0;
        bool skipped = false;
    };

    struct FGExecutionStats {
        double total_cpu_time_ms = 0.0;
        std::vector<FGPassExecutionStat> pass_stats;
    };

    struct FGExecutionContext {
        uint32_t frame_index = 0;
        void* user_context = nullptr;

        bool collect_cpu_timings = false;
        bool log_pass_execution = false;
        FGExecutionStats* out_stats = nullptr;
    };

    // -------------------------------------------------------------------------
    // Runtime graph API
    // -------------------------------------------------------------------------

    class FrameGraphPassContext;
    using FGPassExecutor = std::function<void(FrameGraphPassContext&)>;

    struct FGCompiledResource {
        std::string name;
        FGResourceType type = FGResourceType::Texture;

        FGTextureDesc texture{};
        FGBufferDesc buffer{};
        FGImportedTargetKind imported_kind = FGImportedTargetKind::Swapchain;

        uint32_t resolved_width = 0;
        uint32_t resolved_height = 0;

        Ref<Framebuffer> framebuffer;

        // Transient physical framebuffer allocation index for texture resources.
        // Multiple logical resources may alias the same physical allocation.
        uint32_t physical_allocation = k_invalid_physical;

        FGPassHandle first_use = k_invalid_pass;
        FGPassHandle last_use  = k_invalid_pass;
    };

    struct FGCompiledResourceBinding {
        FGResourceHandle handle = k_invalid_resource;
        FGResourceUsage usage = FGResourceUsage::Unknown;
    };

    struct FGCompiledPass {
        std::string name;

        std::string executor_id;
        FGPassExecutor executor;

        FGQueueDomain queue_domain = FGQueueDomain::Graphics;

        std::vector<FGResourceHandle> reads;
        std::vector<FGResourceHandle> writes;
        std::vector<FGCompiledResourceBinding> read_bindings;
        std::vector<FGCompiledResourceBinding> write_bindings;

        YAML::Node clear_node;
        YAML::Node params_node;

        bool targets_swapchain = false;
        Ref<Framebuffer> target_framebuffer;

        // Physical framebuffer allocation used by this pass when writing transient textures.
        uint32_t physical_allocation = k_invalid_physical;
    };

    class FrameGraphCompiled {
    public:
        bool valid() const;
        void execute(const FGExecutionContext& execution_context = {});

        std::string debug_dump() const;
        void log_debug_dump(std::string_view context_label = "FrameGraph") const;

        const std::vector<FGCompiledPass>& passes() const { return m_passes; }
        const std::vector<FGCompiledResource>& resources() const { return m_resources; }

        FGResourceHandle find_resource_handle(const std::string& name) const;

    private:
        friend class FrameGraphCompiler;
        friend class FrameGraphPassContext;

        std::vector<FGCompiledPass> m_passes;
        std::vector<FGCompiledResource> m_resources;
        std::unordered_map<std::string, FGResourceHandle> m_resource_name_to_handle;

        uint32_t m_logical_framebuffer_allocations = 0;
        uint32_t m_physical_framebuffer_allocations = 0;
    };

    class FrameGraphPassContext {
    public:
        FrameGraphPassContext(FrameGraphCompiled& graph, FGCompiledPass& pass, const FGExecutionContext& execution_context);

        const std::string& pass_name() const;
        uint32_t frame_index() const;
        FGQueueDomain queue_domain() const;

        bool is_graphics_queue() const { return queue_domain() == FGQueueDomain::Graphics; }
        bool is_compute_queue() const { return queue_domain() == FGQueueDomain::Compute; }
        bool is_transfer_queue() const { return queue_domain() == FGQueueDomain::Transfer; }

        const YAML::Node& params() const;
        const YAML::Node& clear() const;

        Ref<Framebuffer> get_input_framebuffer(const std::string& resource_name) const;
        Ref<Framebuffer> get_output_framebuffer(const std::string& resource_name) const;
        Ref<Framebuffer> get_pass_target_framebuffer() const;

        // Vulkan-only helper for compute/transfer frame-graph passes.
        // Records and submits a one-time command buffer on the selected queue domain.
        bool submit_vulkan_compute(const FGVulkanRecordCommands& record) const;
        bool submit_vulkan_transfer(const FGVulkanRecordCommands& record) const;

        void* user_context() const;

        template<typename T>
        T* user_context_as() const {
            return reinterpret_cast<T*>(user_context());
        }

    private:
        FrameGraphCompiled* m_graph = nullptr;
        FGCompiledPass* m_pass = nullptr;
        uint32_t m_frame_index = 0;
        const FGExecutionContext* m_exec_context = nullptr;
    };

    class FrameGraphCompiler {
    public:
        static std::shared_ptr<FrameGraphCompiled>
        compile(const FGGraphDesc& desc,
                FGCompileDiagnostics& out_diagnostics,
                const FGCompileOptions* options = nullptr);
    };


}

