#include "hnpch.h"
#include "renderer_3d.h"
#include "render_command.h"
#include <glm/gtc/matrix_transform.hpp>

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {

    struct Renderer3DStorage {

        Renderer3D::Statistics stats;
    };

    static Renderer3DStorage* s_data;

    void Renderer3D::init() {
        HN_PROFILE_FUNCTION();

        s_data = new Renderer3DStorage;

    }

    void Renderer3D::shutdown() {
        HN_PROFILE_FUNCTION();

        delete s_data;
    }

    void Renderer3D::begin_scene(const PerspectiveCamera& camera) {
        HN_PROFILE_FUNCTION();

    }

    void Renderer3D::end_scene() {
        HN_PROFILE_FUNCTION();

    }

    Renderer3D::Statistics Renderer3D::get_stats() {
        return s_data->stats;
    }

    void Renderer3D::reset_stats() {
        memset(&s_data->stats, 0, sizeof(Statistics));
    }

}