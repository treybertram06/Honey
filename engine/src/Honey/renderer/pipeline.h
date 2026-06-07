#pragma once

#include "pipeline_spec.h"
#include "Honey/core/base.h"

namespace Honey {

    class Pipeline {
    public:
        virtual ~Pipeline() = default;

        const PipelineSpec& get_spec() const { return m_spec; }

        static Ref<Pipeline> create(const PipelineSpec& spec, void* native_render_pass);
        static Ref<Pipeline> create(const PipelineSpec& spec, void* native_render_pass,
                                    void* extra_set_layout);
        static Ref<Pipeline> create(const std::filesystem::path& path, void* native_render_pass);
        // Creates a pipeline whose layout includes a second descriptor set (e.g. font SSBOs at set 1).
        static Ref<Pipeline> create(const std::filesystem::path& path, void* native_render_pass,
                                    void* extra_set_layout);
        // Creates a heap-mode pipeline (VK_EXT_descriptor_heap): null layout, descriptors sourced
        // from the bound heaps via reflection. Drive its descriptors with FrameGraphPassContext.
        static Ref<Pipeline> create_heap_mode(const PipelineSpec& spec, void* native_render_pass);

        virtual void* get_native_pipeline() const = 0;
        virtual void* get_native_pipeline_layout() const = 0;

        // True if built with VK_EXT_descriptor_heap mapping (null layout, descriptors from the heap).
        virtual bool is_heap_mode() const { return false; }

    protected:
        PipelineSpec m_spec;
    };

} // namespace Honey
