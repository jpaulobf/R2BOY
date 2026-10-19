#pragma once

#include <array>
#include <cstdint>
#include "Core/Memory/Memory.h"
#include "Core/IO/Interrupts.h"

namespace R2BOY::Core::PPU
{
    /**
     * PPU por dot: seleção de OAM, fetcher de tiles e FIFOs BG/OBJ independentes.
     * Mistura um pixel por dot quando o fetcher não está parado. Sem SDL no Core.
     * tick() recebe dots de vídeo, não T-cycles de CPU em double speed.
     * Ver docs/PPU.md para o perfil de temporização e limites desta implementação.
     */
    class PPU
    {
    public:
        static constexpr unsigned Width = 160, Height = 144;
        // Pixels 0xAARRGGBB; o frame publicado só muda em VBlank ou ao desligar LCD.
        using Framebuffer = std::array<std::uint32_t, Width * Height>;

        void reset(bool colorHardware = false, bool colorMode = false, bool afterBoot = true);
        void tick(const Memory::Memory &memory, IO::Interrupts &interrupts, bool dmaActive = false);
        std::uint8_t read(std::uint16_t address) const;
        void write(std::uint16_t address, std::uint8_t value, IO::Interrupts &interrupts);
        bool enabled() const { return (lcdc_ & 0x80) != 0; }
        unsigned mode() const { return mode_; }
        unsigned dot() const { return dot_; }
        bool blocksVRAM() const { return enabled() && mode_ == 3; }
        bool blocksOAM() const { return enabled() && (mode_ == 2 || mode_ == 3); }
        const Framebuffer &framebuffer() const { return front_; }
        std::uint64_t frameNumber() const { return frameNumber_; }

    private:
        struct Pixel
        {
            std::uint8_t color = 0, palette = 0, priority = 0;
        };
        struct ObjectPixel
        {
            std::uint8_t color = 0, palette = 0, index = 0xFF, x = 0xFF;
            bool behind = false;
        };
        struct Sprite
        {
            std::uint8_t y = 0, x = 0, tile = 0, attributes = 0, index = 0;
        };

        std::uint8_t ly() const;
        void updateSTAT(IO::Interrupts &interrupts);
        void beginLine();
        void beginTransfer();
        void scanOAM(const Memory::Memory &memory, bool dmaActive);
        void transfer(const Memory::Memory &memory, IO::Interrupts &interrupts);
        void fetchBackground(const Memory::Memory &memory);
        bool pushBackground();
        bool startObject();
        void fetchObject(const Memory::Memory &memory);
        void mergeObject();
        std::uint32_t mix(Pixel background, const ObjectPixel &object) const;
        std::uint32_t paletteColor(bool object, unsigned palette, unsigned color) const;

        Framebuffer front_{}, back_{};
        std::array<std::uint8_t, 64> bgColors_{}, objColors_{};
        std::array<Pixel, 16> bgFIFO_{};
        std::array<ObjectPixel, 8> objFIFO_{};
        std::array<Sprite, 10> sprites_{};
        Sprite fetchingObject_{};
        std::uint8_t lcdc_ = 0, stat_ = 0, scy_ = 0, scx_ = 0, lyc_ = 0;
        std::uint8_t bgp_ = 0xFC, obp0_ = 0xFF, obp1_ = 0xFF, wy_ = 0, wx_ = 0;
        std::uint8_t bgIndex_ = 0, objIndex_ = 0, opri_ = 0;
        std::uint8_t tile_ = 0, attributes_ = 0, tileY_ = 0, low_ = 0, high_ = 0;
        std::uint8_t objectLow_ = 0, objectHigh_ = 0;
        std::uint16_t tileAddress_ = 0, objectAddress_ = 0;
        unsigned line_ = 0, dot_ = 0, mode_ = 0, x_ = 0;
        unsigned spriteCount_ = 0, nextSprite_ = 0, objectTicks_ = 0;
        unsigned bgHead_ = 0, bgSize_ = 0, fetchStep_ = 0, fetchTicks_ = 0, fetchX_ = 0;
        unsigned warmup_ = 0, discard_ = 0, windowDelay_ = 0, windowLine_ = 0;
        int lastObjectTile_ = -1;
        std::uint64_t frameNumber_ = 0;
        bool colorHardware_ = false, colorMode_ = false, statLine_ = false;
        bool windowTriggered_ = false, windowActive_ = false, windowDrawn_ = false;
        bool fastWindowFetch_ = false, blankFirstFrame_ = false;
    };
}