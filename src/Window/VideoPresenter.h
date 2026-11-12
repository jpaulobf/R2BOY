#pragma once
#include "Common/Framebuffer.h"
#include "DisplayMenu.h"
#include <SDL3/SDL.h>
#include <memory>
#include <vector>

namespace R2BOY::Window
{
    class NtscShaderPipeline;

    // Possui texturas, buffers e pipeline usados na apresentação do vídeo.
    class VideoPresenter
    {
    public:
        VideoPresenter();
        ~VideoPresenter();
        bool initialize(SDL_Renderer *renderer);
        bool prepare(SDL_Window *window, SDL_Renderer *renderer, DisplaySettings &display, bool gameLoaded);
        SDL_Texture *texture(bool shaderActive) const;
        bool upload(SDL_Window *window, DisplaySettings &display, const PalettePreset &preset,
                    bool gameLoaded, bool dmgMode, bool shaderActive, const Common::Framebuffer &framebuffer);

    private:
        std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture_{nullptr, SDL_DestroyTexture};
        std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> shaderTexture{nullptr, SDL_DestroyTexture};
        std::unique_ptr<NtscShaderPipeline> shaderPipeline;
        ShaderType initializedShader = ShaderType::None;
        int shaderWidth = 0, shaderHeight = 0;
        std::vector<std::uint32_t> shaderPixels;
        Common::Framebuffer filteredFrame{};
    };
}