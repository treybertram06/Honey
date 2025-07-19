#include "hnpch.h"
#include "framebuffer.h"

#include "Honey/renderer/renderer.h"
#include "platform/metal/metal_framebuffer.h"
#include "platform/opengl/opengl_framebuffer.h"

namespace Honey {

	Ref<Framebuffer> Framebuffer::create(const FramebufferSpecification &spec) {

		switch (Renderer::get_api()) {
			case RendererAPI::API::none: HN_CORE_ASSERT(false, "RendererAPI::none is not supported!"); return nullptr;
			case RendererAPI::API::opengl: return CreateRef<OpenGLFramebuffer>(spec);
			case RendererAPI::API::metal: return CreateRef<MetalFramebuffer>(spec);
		}

		HN_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}


}
