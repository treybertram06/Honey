#pragma once

// Core engine
#include "Honey/core/engine.h"
#include "Honey/renderer/renderer.h"
#include "Honey/renderer/renderer_2d.h"
#include "Honey/renderer/renderer_3d.h"
#include "Honey/renderer/render_command.h"

#include "Honey/core/timestep.h"

// Renderer abstractions
#include "Honey/renderer/buffer.h"
#include "Honey/renderer/vertex_array.h"
#include "Honey/renderer/shader.h"
#include "Honey/renderer/camera.h"
#include "Honey/renderer/texture.h"
#include "Honey/renderer/framebuffer.h"
#include "Honey/camera_controller.h"

// Layering & UI
#include "Honey/core/layer.h"
#include "Honey/imgui/imgui_layer.h"

// Input & events
#include "Honey/core/input.h"
#include "Honey/core/keycodes.h"
#include "Honey/core/mouse_button_codes.h"
#include "Honey/events/event.h"

// Logging
#include "Honey/core/log.h"
#include <spdlog/fmt/ostr.h>

// Entry Point
//#include "Honey/entry_point.h"
