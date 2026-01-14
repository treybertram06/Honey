#pragma once

#include <filesystem>

#include "Honey/core/timestep.h"

namespace Honey {

    class AudioSystem {
    public:
        static void init();
        static void shutdown();

        static void on_update(Timestep ts);

        using Handle = void*;

        static Handle create_source(const std::filesystem::path& filepath);
        static void destroy_source(Handle handle);

        static void play(Handle handle);
        static void play_at(Handle handle, float time /* seconds */);
        static void stop(Handle handle);

        static void set_looping(Handle handle, bool looping);
        static void set_volume(Handle handle, float volume);
        static void set_pitch(Handle handle, float pitch);

        static bool is_valid(Handle handle);
    };

}
