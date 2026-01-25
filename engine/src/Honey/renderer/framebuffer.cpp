#include "hnpch.h"
#include "framebuffer.h"

#include "Honey/renderer/renderer.h"
#include "platform/opengl/opengl_framebuffer.h"
#include "platform/vulkan/vk_framebuffer.h"

namespace Honey {
	Ref<Framebuffer> Framebuffer::create(const FramebufferSpecification &spec) {
		return RenderCommand::get_renderer_api()->create_framebuffer(spec);
	}


}
