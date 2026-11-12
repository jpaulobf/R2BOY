#include "VideoEffects.h"

namespace R2BOY::Window
{
    const Common::Framebuffer &prepareVideoFrame(const Common::Framebuffer &framebuffer, bool gameLoaded,
                                                 bool dmgMode, const DisplaySettings &display, const PalettePreset &preset, Common::Framebuffer &output)
    {
        constexpr int screenWidth = Common::Constants::ScreenWidth;
        constexpr int screenHeight = Common::Constants::ScreenHeight;
        const Common::Framebuffer *textureFrame = &framebuffer;
        if (gameLoaded && dmgMode && display.palettePreset != 0)
        {
            // Recolore o resultado de BGP/OBP sem alterar a PPU ou os save states.
            // Também atualiza imediatamente o quadro congelado durante a pausa.
            output = framebuffer;
            for (std::size_t i = 0; i < output.size(); ++i)
            {
                // Cada posição do bloco 2x2 usa quatro cores, fixas entre frames.
                const auto &colors = preset.pixelColors
                                         ? (*preset.pixelColors)[(i / screenWidth % 2) * 2 + (i % screenWidth % 2)]
                                         : preset.colors;
                auto &pixel = output[i];
                switch (pixel)
                {
                case 0xFFFFFFFF:
                    pixel = colors[0];
                    break;
                case 0xFFAAAAAA:
                    pixel = colors[1];
                    break;
                case 0xFF555555:
                    pixel = colors[2];
                    break;
                case 0xFF000000:
                    pixel = colors[3];
                    break;
                default:
                    break;
                }
            }
            textureFrame = &output;
        }
        if (display.scanlines)
        {
            // Escurece linhas alternadas na textura, preservando o framebuffer do Core.
            output = *textureFrame;
            const unsigned brightness = 100 - display.scanlinesLevel;
            for (int y = 1; y < screenHeight; y += 2)
            {
                for (int x = 0; x < screenWidth; ++x)
                {
                    auto &pixel = output[y * screenWidth + x];
                    const unsigned red = (((pixel >> 16) & 0xFF) * brightness + 50) / 100;
                    const unsigned green = (((pixel >> 8) & 0xFF) * brightness + 50) / 100;
                    const unsigned blue = ((pixel & 0xFF) * brightness + 50) / 100;
                    pixel = (pixel & 0xFF000000u) | (red << 16) | (green << 8) | blue;
                }
            }
            textureFrame = &output;
        }
        return *textureFrame;
    }
}