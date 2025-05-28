#pragma once

// Core engine
#include "Honey/engine.h"
#include "Honey/renderer/renderer.h"
#include "Honey/renderer/render_command.h"

#include "Honey/core/timestep.h"

// Renderer abstractions
#include "Honey/renderer/buffer.h"
#include "Honey/renderer/vertex_array.h"
#include "Honey/renderer/shader.h"
#include "Honey/renderer/camera.h"

// Layering & UI
#include "Honey/layer.h"
#include "Honey/imgui/imgui_layer.h"

// Input & events
#include "Honey/input.h"
#include "Honey/keycodes.h"
#include "Honey/mouse_button_codes.h"
#include "Honey/events/event.h"

// Logging
#include "Honey/log.h"
#include <spdlog/fmt/ostr.h>

// Entry Point
//#include "Honey/entry_point.h"
