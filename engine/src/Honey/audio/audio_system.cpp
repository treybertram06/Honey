#include "hnpch.h"
#include "audio_system.h"

#include <soloud.h>
#include <soloud_wav.h>

#include "soloud_speech.h"

namespace Honey {

    namespace {
        SoLoud::Soloud* s_engine = nullptr;
        
        struct SourceData {
            SoLoud::Wav wav;
            bool loop = false;
            float volume = 1.0f;
            float pitch = 1.0f;
            SoLoud::handle last_handle = 0; // 0 = invalid
        };
        
        inline SourceData* as_source(AudioSystem::Handle handle) {
            return static_cast<SourceData*>(handle);
        }
    }

void AudioSystem::init() {
        HN_CORE_INFO("AudioSystem: init (SoLoud)");

        if (s_engine) {
            HN_CORE_WARN("AudioSystem::init called twice");
            return;
        }

        s_engine = new SoLoud::Soloud();

        SoLoud::result res = s_engine->init();
        if (res != SoLoud::SO_NO_ERROR) {
            HN_CORE_ERROR("SoLoud init failed (code {}): {}", (int)res, s_engine->getErrorString(res));
            delete s_engine;
            s_engine = nullptr;
            return;
        }
    }

    void AudioSystem::shutdown() {
        HN_CORE_INFO("AudioSystem: shutdown (SoLoud)");
        if (!s_engine)
            return;

        s_engine->stopAll();
        s_engine->deinit();

        delete s_engine;
        s_engine = nullptr;
    }

    void AudioSystem::on_update(Timestep ts) {
        (void)ts;
        //if (s_engine) {
            //HN_CORE_INFO("SoLoud voices: {}", s_engine->getActiveVoiceCount());
        //}
    }

    AudioSystem::Handle AudioSystem::create_source(const std::filesystem::path& filepath) {
        if (!s_engine) {
            HN_CORE_WARN("AudioSystem::create_source called before init");
            return nullptr;
        }

        auto* src = new SourceData();

        SoLoud::result res = src->wav.load(filepath.string().c_str());
        if (res != SoLoud::SO_NO_ERROR) {
            HN_CORE_ERROR("Failed to load audio '{}': {}", filepath.string(), s_engine->getErrorString(res));
            delete src;
            return nullptr;
        }

        HN_CORE_INFO("AudioSystem: loaded '{}'", filepath.string());
        return static_cast<Handle>(src);
    }

    void AudioSystem::destroy_source(Handle handle) {
        if (!handle)
            return;

        auto* src = as_source(handle);

        if (s_engine && src->last_handle != 0) {
            s_engine->stop(src->last_handle);
        }

        delete src;
    }

    void AudioSystem::play(Handle handle) {
        HN_CORE_INFO("PLAY CALLED");
        if (!s_engine || !handle) {
            if (!s_engine)
                HN_CORE_WARN("AudioSystem::play engine not initialized!");
            if (!handle)
                HN_CORE_WARN("AudioSystem::play called with null handle!");
            return;
        }

        auto* src = as_source(handle);

        src->wav.setLooping(src->loop);

        // pitch -> relativePlaySpeed, volume -> volume
        SoLoud::handle h = s_engine->play(src->wav, src->volume, 0.0f, false/*, src->pitch*/);
        src->last_handle = h;
        HN_CORE_INFO("Playing audio...");
    }

    void AudioSystem::play_at(Handle handle, float time) {
        if (!s_engine || !handle)
            return;

        auto* src = as_source(handle);

        src->wav.setLooping(src->loop);

        // Start paused with desired pitch/volume, then seek and unpause
        SoLoud::handle h = s_engine->playClocked(0.0f, src->wav, src->volume/*, src->pitch*/);
        s_engine->seek(h, time);
        src->last_handle = h;
    }

    void AudioSystem::stop(Handle handle) {
        if (!s_engine || !handle)
            return;

        auto* src = as_source(handle);

        if (src->last_handle != 0) {
            s_engine->stop(src->last_handle);
            src->last_handle = 0;
        }

        // Also stop any other instances of this sound for safety.
        s_engine->stopAudioSource(src->wav);
    }

    void AudioSystem::set_looping(Handle handle, bool looping) {
        if (!s_engine || !handle)
            return;

        auto* src = as_source(handle);
        src->loop = looping;
        src->wav.setLooping(looping);
    }

    void AudioSystem::set_volume(Handle handle, float volume) {
        if (!s_engine || !handle)
            return;

        auto* src = as_source(handle);
        src->volume = volume;

        if (src->last_handle != 0) {
            s_engine->setVolume(src->last_handle, volume);
        }
    }

    void AudioSystem::set_pitch(Handle handle, float pitch) {
        if (!handle)
            return;

        auto* src = as_source(handle);
        src->pitch = pitch;

        if (s_engine && src->last_handle != 0) {
            // Update current voice's relative play speed
            s_engine->setRelativePlaySpeed(src->last_handle, pitch);
        }
    }

    bool AudioSystem::is_valid(Handle handle) {
        return handle != nullptr;
    }
    
}
