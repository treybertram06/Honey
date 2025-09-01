#pragma once

namespace Honey {

	enum class FramebufferTextureFormat {
		None = 0,

		//color
		RGBA8,
	    RED_INTEGER,

		//depth/stencil
		DEPTH24STENCIL8,

		//defaults
		Depth = DEPTH24STENCIL8
	};

	struct FramebufferTextureSpecification {
		FramebufferTextureSpecification() = default;
		FramebufferTextureSpecification(FramebufferTextureFormat format)
			: texture_format(format) {}

		FramebufferTextureFormat texture_format = FramebufferTextureFormat::None;
		//TODO: filtering/wrap
	};

    struct FramebufferAttachmentSpecification {
        FramebufferAttachmentSpecification() = default;
        FramebufferAttachmentSpecification(std::initializer_list<FramebufferTextureSpecification> attachments)
            : attachments(attachments) {}

        std::vector<FramebufferTextureSpecification> attachments;
    };

	struct FramebufferSpecification {
		uint32_t width, height;
	    FramebufferAttachmentSpecification attachments;
		uint32_t samples = 1;

		bool swap_chain_target = false;

	};

	class Framebuffer {
	public:

	    virtual ~Framebuffer() = default;

		virtual void bind() = 0;
		virtual void unbind() = 0;

	    virtual void resize(uint32_t width, uint32_t height) = 0;
	    virtual int read_pixel(uint32_t attachment_index, int x, int y) = 0;
	    virtual void clear_attachment(uint32_t attachment_index, const void* value) = 0;

		virtual uint32_t get_color_attachment_renderer_id(uint32_t index = 0) const = 0;

		//virtual FramebufferSpecification& get_specification() = 0;
		virtual const FramebufferSpecification& get_specification() const = 0;
		static Ref<Framebuffer> create(const FramebufferSpecification& spec);

	};
}