#include "VideoPresenter.h"
#include "VideoEffects.h"
#include "NtscShaderPipeline.h"
#include <filesystem>
#include <iostream>
#include <string>

namespace R2BOY::Window
{
    namespace
    {
        constexpr int screenWidth = Common::Constants::ScreenWidth;
        constexpr int screenHeight = Common::Constants::ScreenHeight;
    }
    VideoPresenter::VideoPresenter() = default;
    VideoPresenter::~VideoPresenter() = default;

    bool VideoPresenter::initialize(SDL_Renderer *renderer)
    {
        texture_.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, screenWidth, screenHeight));
        if (!texture_ || !SDL_SetTextureScaleMode(texture_.get(), SDL_SCALEMODE_NEAREST))
        {
            std::cerr << "Failed to create display texture: " << SDL_GetError() << '\n';
            return false;
        }

        return true;
    }

    bool VideoPresenter::prepare(SDL_Window *window, SDL_Renderer *renderer, DisplaySettings &display, bool gameLoaded)
    {
        const ShaderType selectedShader = display.shader;
        bool shaderActive = selectedShader != ShaderType::None && gameLoaded;
        if (shaderActive && initializedShader != selectedShader)
        {
            const char *preset = selectedShader == ShaderType::Gauss ? "shaders/ntsc/ntsc-160px-gauss-scanline.cgp" : selectedShader == ShaderType::HQ2 ? "shaders/hqx/hq2x-visual.cgp"
                                                                                                                  : selectedShader == ShaderType::CRT   ? "shaders/crt/crt-hyllian-3d-nes.cgp"
                                                                                                                  : selectedShader == ShaderType::CRT3D ? "shaders/crt/crt-lottes.cgp"
                                                                                                                                                        : "shaders/scalefx/scalefx-visual.cgp";
            auto candidate = std::make_unique<NtscShaderPipeline>();
            std::string error;
            const char *base = SDL_GetBasePath();
            if (!candidate->initialize(std::filesystem::path(base ? base : "") / preset, error))
            {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Shaders", error.c_str(), window);
                display.shader = ShaderType::None;
                shaderActive = false;
            }
            else
            {
                shaderPipeline = std::move(candidate);
                initializedShader = selectedShader;
            }
        }
        if (shaderActive && (!shaderTexture || shaderWidth != display.imageWidth || shaderHeight != display.imageHeight))
        {
            auto replacement = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>(
                SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  display.imageWidth, display.imageHeight),
                SDL_DestroyTexture);
            // Alguns shaders retornam apenas RGB e deixam o alfa em zero.
            // A imagem do jogo é opaca, portanto não deve usar esse alfa na composição.
            if (!replacement || !SDL_SetTextureBlendMode(replacement.get(), SDL_BLENDMODE_NONE))
            {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Shaders", SDL_GetError(), window);
                display.shader = ShaderType::None;
                shaderActive = false;
            }
            else
            {
                shaderTexture = std::move(replacement);
                shaderWidth = display.imageWidth;
                shaderHeight = display.imageHeight;
            }
        }
        return shaderActive;
    }

    SDL_Texture *VideoPresenter::texture(bool shaderActive) const
    {
        return shaderActive ? shaderTexture.get() : texture_.get();
    }

    bool VideoPresenter::upload(SDL_Window *window, DisplaySettings &display, const PalettePreset &preset,
                                bool gameLoaded, bool dmgMode, bool shaderActive, const Common::Framebuffer &framebuffer)
    {
        const auto &preparedFrame = prepareVideoFrame(framebuffer, gameLoaded, dmgMode, display,
                                                      preset, filteredFrame);
        const auto *textureFrame = &preparedFrame;
        if (!SDL_UpdateTexture(texture_.get(), nullptr, textureFrame->data(), screenWidth * sizeof(std::uint32_t)))
        {
            std::cerr << "Failed to update display texture: " << SDL_GetError() << '\n';
            return false;
        }
        if (shaderActive)
        {
            std::string error;
            const bool rendered = shaderPipeline->render(textureFrame->data(), screenWidth, {screenWidth, screenHeight},
                                                         {shaderWidth, shaderHeight}, shaderPixels, error);
            if (rendered)
            {
                // A tela do jogo é opaca, inclusive quando o shader retorna RGB sem alfa.
                for (auto &pixel : shaderPixels)
                    pixel |= 0xff000000u;
            }
            if (!rendered || !SDL_UpdateTexture(shaderTexture.get(), nullptr, shaderPixels.data(), shaderWidth * sizeof(std::uint32_t)))
            {
                if (error.empty())
                    error = SDL_GetError();
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Shaders", error.c_str(), window);
                display.shader = ShaderType::None;
            }
        }
        return true;
    }
}