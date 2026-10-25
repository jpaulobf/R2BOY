#include "Audio.h"

#include <SDL3/SDL.h>
#include <iostream>

namespace R2BOY::System
{
    bool Audio::open()
    {
        if (stream_)
            return true;
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            std::cerr << "Audio: Unavailable: " << SDL_GetError() << '\n';
            return false;
        }
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, static_cast<int>(sampleRate_)};
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream_)
        {
            std::cerr << "Audio: Unable to open the playback device: " << SDL_GetError() << '\n';
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }
        // O stream nasce pausado.
        SDL_ResumeAudioStreamDevice(stream_);
        return true;
    }

    void Audio::close()
    {
        if (!stream_)
            return;
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    void Audio::queue(const std::vector<float> &samples)
    {
        if (!stream_ || !enabled_ || samples.empty())
            return;
        const int limit = static_cast<int>(sampleRate_ * 2 * sizeof(float) * 150 / 1000);
        if (SDL_GetAudioStreamQueued(stream_) > limit)
            return;
        SDL_PutAudioStreamData(stream_, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
    }

    void Audio::setEnabled(bool enabled)
    {
        if (enabled_ == enabled)
            return;
        enabled_ = enabled;
        clear();
    }

    void Audio::clear()
    {
        if (stream_)
            SDL_ClearAudioStream(stream_);
    }
}