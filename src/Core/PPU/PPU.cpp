#include "PPU.h"

#include <algorithm>

namespace R2BOY::Core::PPU
{
    void PPU::reset(bool colorHardware, bool colorMode, bool afterBoot)
    {
        const bool unlimitedSprites = unlimitedSprites_;
        *this = PPU{};
        unlimitedSprites_ = unlimitedSprites;
        colorHardware_ = colorHardware;
        colorMode_ = colorMode;
        lcdc_ = afterBoot ? 0x91 : 0;
        mode_ = afterBoot ? 2 : 0;
        front_.fill(0xFFFFFFFF);
        back_.fill(0xFFFFFFFF);
        bgColors_.fill(0xFF);
        objColors_.fill(0xFF); // Perfil determinístico; OBJ CRAM real não é inicializada.
        beginLine();
    }

    std::uint8_t PPU::ly() const
    {
        return static_cast<std::uint8_t>(line_ == 153 && dot_ >= 4 ? 0 : line_);
    }

    void PPU::updateSTAT(IO::Interrupts &interrupts)
    {
        // O comparador para com LCD off: LY volta a zero, mas a coincidência
        // retém o resultado anterior até o LCD ser ligado novamente (Mooneye).
        if (enabled())
            coincidence_ = ly() == lyc_;
        // As fontes compartilham uma única linha: trocar de fonte sem baixar o sinal
        // não produz uma segunda interrupção de STAT.
        const auto selections = statWriteTicks_ ? 0x78 : stat_;
        const bool signal = ((selections & 0x40) && coincidence_) || (enabled() && (
                                          ((selections & 0x20) && mode_ == 2) ||
                                          ((selections & 0x10) && mode_ == 1) ||
                                          ((selections & 0x08) && mode_ == 0 && !(lcdStartup_ && dot_ < 80))));
        if (signal && !statLine_)
            interrupts.request(1);
        statLine_ = signal;
    }

    std::uint8_t PPU::read(std::uint16_t address) const
    {
        switch (address)
        {
        case 0xFF40:
            return lcdc_;
        case 0xFF41:
            return 0x80 | stat_ | (coincidence_ ? 4 : 0) | mode_;
        case 0xFF42:
            return scy_;
        case 0xFF43:
            return scx_;
        case 0xFF44:
            return ly();
        case 0xFF45:
            return lyc_;
        case 0xFF47:
            return bgp_;
        case 0xFF48:
            return obp0_;
        case 0xFF49:
            return obp1_;
        case 0xFF4A:
            return wy_;
        case 0xFF4B:
            return wx_;
        case 0xFF68:
            return colorMode_ ? bgIndex_ | 0x40 : 0xFF;
        case 0xFF69:
            return colorMode_ && !blocksVRAM() ? bgColors_[bgIndex_ & 63] : 0xFF;
        case 0xFF6A:
            return colorMode_ ? objIndex_ | 0x40 : 0xFF;
        case 0xFF6B:
            return colorMode_ && !blocksVRAM() ? objColors_[objIndex_ & 63] : 0xFF;
        case 0xFF6C:
            return colorMode_ ? 0xFE | opri_ : 0xFF;
        default:
            return 0xFF;
        }
    }

    void PPU::write(std::uint16_t address, std::uint8_t value, IO::Interrupts &interrupts)
    {
        switch (address)
        {
        case 0xFF40:
        {
            const bool wasEnabled = enabled();
            lcdc_ = value;
            if (wasEnabled != enabled())
            {
                line_ = dot_ = windowLine_ = 0;
                windowTriggered_ = false;
                // A primeira linha após LCD on apresenta modo 0 antes do 3;
                // não é uma linha normal de busca de OAM (Mooneye lcdon_timing).
                mode_ = 0;
                bgSize_ = objectTicks_ = 0;
                objectWaiting_ = false;
                objectWait_ = 0;
                statWriteTicks_ = 0;
                front_.fill(0xFFFFFFFF);
                back_.fill(0xFFFFFFFF);
                blankFirstFrame_ = enabled();
                lcdStartup_ = enabled();
                beginLine();
            }
            break;
        }
        case 0xFF41:
            // No DMG físico as seleções se comportam como FF por um M-cycle.
            // O valor lido já é o escrito; somente a linha interna recebe o pulso.
            if (!colorHardware_ && enabled())
                statWriteTicks_ = 4;
            stat_ = value & 0x78;
            break;
        case 0xFF42:
            scy_ = value;
            break;
        case 0xFF43:
            scx_ = value;
            break;
        case 0xFF45:
            lyc_ = value;
            break;
        case 0xFF47:
            bgp_ = value;
            break;
        case 0xFF48:
            obp0_ = value;
            break;
        case 0xFF49:
            obp1_ = value;
            break;
        case 0xFF4A:
            wy_ = value;
            break;
        case 0xFF4B:
            wx_ = value;
            break;
        case 0xFF68:
            if (colorMode_)
                bgIndex_ = value & 0xBF;
            break;
        case 0xFF6A:
            if (colorMode_)
                objIndex_ = value & 0xBF;
            break;
        case 0xFF69:
        case 0xFF6B:
            if (colorMode_)
            {
                auto &index = address == 0xFF69 ? bgIndex_ : objIndex_;
                auto &colors = address == 0xFF69 ? bgColors_ : objColors_;
                if (!blocksVRAM())
                    colors[index & 63] = value;
                // Auto-incremento ocorre mesmo quando a escrita é bloqueada.
                if (index & 0x80)
                    index = 0x80 | ((index + 1) & 63);
            }
            break;
        case 0xFF6C:
            if (colorMode_)
                opri_ = value & 1;
            break;
        default:
            break; // LY é somente leitura; FF46 pertence ao DMA.
        }
        updateSTAT(interrupts);
    }

    void PPU::beginLine()
    {
        spriteCount_ = nextSprite_ = 0;
        extraSpriteCount_ = 0;
        windowPixel_ = 0;
        windowActive_ = windowDrawn_ = false;
        if (line_ < Height && line_ == wy_)
            windowTriggered_ = true;
    }

    void PPU::scanOAM(const Memory::Memory &memory, bool dmaActive)
    {
        if ((dot_ & 1) != 0 || (spriteCount_ == 10 && !unlimitedSprites_) || dmaActive)
            return;
        const auto index = (dot_ / 2) - 1;
        const auto base = static_cast<std::uint16_t>(0xFE00 + index * 4);
        const auto y = memory.read(base);
        const unsigned height = (lcdc_ & 4) ? 16 : 8;
        if (line_ + 16 < y || line_ + 16 >= y + height)
            return;
        const Sprite sprite{y, memory.read(base + 1), memory.read(base + 2),
                            memory.read(base + 3), static_cast<std::uint8_t>(index)};
        if (spriteCount_ < sprites_.size())
            sprites_[spriteCount_++] = sprite;
        else if (extraSpriteCount_ < extraSprites_.size())
            extraSprites_[extraSpriteCount_++] = sprite;
    }

    void PPU::beginTransfer()
    {
        x_ = nextSprite_ = objectTicks_ = bgHead_ = bgSize_ = 0;
        objectWaiting_ = false;
        objectWait_ = 0;
        bgSize_ = 8; // Pixels de partida descartados antes do primeiro pixel visível.
        bgFIFO_.fill(Pixel{});
        fetchStep_ = fetchTicks_ = fetchX_ = 0;
        warmup_ = 12;
        discard_ = scx_ & 7;
        windowDelay_ = 0;
        objFIFO_.fill(ObjectPixel{});
        std::sort(sprites_.begin(), sprites_.begin() + spriteCount_, [](const Sprite &a, const Sprite &b)
                  { return a.x < b.x || (a.x == b.x && a.index < b.index); });
    }

    bool PPU::pushBackground()
    {
        if (bgSize_ != 0)
            return false;
        for (unsigned i = 0; i < 8; ++i)
        {
            const unsigned bit = (attributes_ & 0x20) ? i : 7 - i;
            bgFIFO_[(bgHead_ + bgSize_++) & 15] = {
                static_cast<std::uint8_t>(((low_ >> bit) & 1) | (((high_ >> bit) & 1) << 1)),
                static_cast<std::uint8_t>(attributes_ & 7), static_cast<std::uint8_t>(attributes_ >> 7)};
        }
        ++fetchX_;
        fetchStep_ = fetchTicks_ = 0;
        return true;
    }

    void PPU::fetchBackground(const Memory::Memory &memory)
    {
        // Três leituras de dois dots. Após high, o push aguarda o FIFO vazio.
        if (fetchStep_ == 3)
        {
            pushBackground();
            return;
        }
        ++fetchTicks_;
        if (fetchStep_ == 0 && fetchTicks_ == 1)
        {
            tileY_ = static_cast<std::uint8_t>(windowActive_ ? windowLine_ : line_ + scy_);
            const auto tileX = windowActive_ ? fetchX_ : fetchX_ + (scx_ >> 3);
            const unsigned map = (lcdc_ & (windowActive_ ? 0x40 : 0x08)) ? 0x9C00 : 0x9800;
            tileMapAddress_ = static_cast<std::uint16_t>(map + (tileY_ / 8) * 32 + (tileX & 31));
        }
        else if ((fetchStep_ == 1 || fetchStep_ == 2) && fetchTicks_ == 1)
        {
            // Perfil CGB-D ou posterior: Y comum aos planos. DMG relê SCY.
            const unsigned y = colorHardware_ ? tileY_ : windowActive_ ? windowLine_ : line_ + scy_;
            unsigned row = y & 7;
            if (attributes_ & 0x40)
                row = 7 - row;
            const int tileBase = (lcdc_ & 0x10) ? 0x8000 + tile_ * 16
                                                : 0x9000 + static_cast<std::int8_t>(tile_) * 16;
            tileAddress_ = static_cast<std::uint16_t>(tileBase + row * 2);
        }
        if (fetchTicks_ < 2)
            return;
        fetchTicks_ = 0;
        if (fetchStep_ == 0)
        {
            tile_ = memory.readVRAM(tileMapAddress_, 0);
            attributes_ = colorMode_ ? memory.readVRAM(tileMapAddress_, 1) : 0;
        }
        else if (fetchStep_ == 1)
            low_ = memory.readVRAM(tileAddress_, (attributes_ >> 3) & 1);
        else if (fetchStep_ == 2)
        {
            high_ = memory.readVRAM(tileAddress_ + 1, (attributes_ >> 3) & 1);
            if (pushBackground())
                return;
        }
        ++fetchStep_;
    }

    bool PPU::startObject()
    {
        while (nextSprite_ < spriteCount_)
        {
            const auto &sprite = sprites_[nextSprite_];
            const int position = warmup_ ? -static_cast<int>(warmup_) : static_cast<int>(x_);
            if (sprite.x > position + 8)
                return false;
            ++nextSprite_;
            if (!(lcdc_ & 2) && !colorHardware_)
                continue;
            fetchingObject_ = sprite;
            objectTicks_ = 6;
            objectWaiting_ = true;
            objectWait_ = sprite.x == 0 ? 5 : 0;
            return true;
        }
        return false;
    }

    void PPU::fetchObject(const Memory::Memory &memory)
    {
        if (objectWaiting_)
        {
            // BG continua até high T2 com pixels disponíveis. Objetos seguintes
            // encontram o estágio de push pronto, sem novo cálculo por SCX/WX.
            const bool ready = bgSize_ != 0 && (fetchStep_ == 3 || (fetchStep_ == 2 && fetchTicks_ == 1));
            if (!ready || objectWait_ != 0)
            {
                fetchBackground(memory);
                if (objectWait_)
                    --objectWait_;
                return;
            }
            objectWaiting_ = false;
        }
        if (objectTicks_ == 6)
            fetchBackground(memory); // Termina o plano alto antes da posse de OBJ.
        // Modo 2 seleciona Y/X; tile e atributos pertencem à busca de OBJ
        // no modo 3. DMA pode alterar OAM entre essas duas etapas.
        if (objectTicks_ == 5)
        {
            const auto base = static_cast<std::uint16_t>(0xFE00 + fetchingObject_.index * 4);
            fetchingObject_.tile = memory.read(base + 2);
            fetchingObject_.attributes = memory.read(base + 3);
        }
        const unsigned bank = colorMode_ ? (fetchingObject_.attributes >> 3) & 1 : 0;
        if (objectTicks_ == 3 || objectTicks_ == 1)
        {
            const unsigned height = (lcdc_ & 4) ? 16 : 8;
            unsigned row = (line_ + 16 - fetchingObject_.y) & (height - 1);
            if (fetchingObject_.attributes & 0x40)
                row = height - 1 - row;
            const unsigned tile = height == 16 ? fetchingObject_.tile & 0xFE : fetchingObject_.tile;
            objectAddress_ = static_cast<std::uint16_t>(0x8000 + tile * 16 + row * 2);
        }
        if (objectTicks_ == 3)
            objectLow_ = memory.readVRAM(objectAddress_, bank);
        if (objectTicks_ == 1)
            objectHigh_ = memory.readVRAM(objectAddress_ + 1, bank);
        if (--objectTicks_ == 0)
            mergeObject();
    }

    void PPU::mergeObject()
    {
        const auto &sprite = fetchingObject_;
        for (unsigned i = 0; i < 8; ++i)
        {
            const int position = warmup_ ? -static_cast<int>(warmup_) : static_cast<int>(x_);
            const int destination = static_cast<int>(sprite.x) - 8 + static_cast<int>(i) - position;
            if (destination < 0 || destination >= 8)
                continue;
            const unsigned bit = (sprite.attributes & 0x20) ? i : 7 - i;
            const auto color = static_cast<std::uint8_t>(((objectLow_ >> bit) & 1) | (((objectHigh_ >> bit) & 1) << 1));
            auto &pixel = objFIFO_[destination];
            const bool coordinatePriority = !colorMode_ || opri_;
            const bool wins = coordinatePriority ? sprite.x < pixel.x || (sprite.x == pixel.x && sprite.index < pixel.index)
                                                 : sprite.index < pixel.index;
            if (color != 0 && (pixel.color == 0 || wins))
                pixel = {color, static_cast<std::uint8_t>(colorMode_ ? sprite.attributes & 7 : (sprite.attributes >> 4) & 1),
                         sprite.index, sprite.x, (sprite.attributes & 0x80) != 0};
        }
    }

    PPU::ObjectPixel PPU::withExtraSprites(const Memory::Memory &memory, ObjectPixel object) const
    {
        if (!unlimitedSprites_ || !(lcdc_ & 2))
            return object;
        for (unsigned index = 0; index < extraSpriteCount_; ++index)
        {
            const auto &sprite = extraSprites_[index];
            const int offset = static_cast<int>(x_) + 8 - sprite.x;
            if (offset < 0 || offset >= 8)
                continue;
            const unsigned height = (lcdc_ & 4) ? 16 : 8;
            unsigned row = (line_ + 16 - sprite.y) & (height - 1);
            if (sprite.attributes & 0x40)
                row = height - 1 - row;
            const unsigned tile = height == 16 ? sprite.tile & 0xFE : sprite.tile;
            const auto address = static_cast<std::uint16_t>(0x8000 + tile * 16 + row * 2);
            const unsigned bank = colorMode_ ? (sprite.attributes >> 3) & 1 : 0;
            const unsigned bit = sprite.attributes & 0x20 ? offset : 7 - offset;
            const unsigned color = ((memory.readVRAM(address, bank) >> bit) & 1) |
                                   (((memory.readVRAM(address + 1, bank) >> bit) & 1) << 1);
            const bool coordinatePriority = !colorMode_ || opri_;
            const bool wins = coordinatePriority ? sprite.x < object.x || (sprite.x == object.x && sprite.index < object.index)
                                                 : sprite.index < object.index;
            if (color != 0 && (object.color == 0 || wins))
                object = {static_cast<std::uint8_t>(color),
                          static_cast<std::uint8_t>(colorMode_ ? sprite.attributes & 7 : (sprite.attributes >> 4) & 1),
                          sprite.index, sprite.x, (sprite.attributes & 0x80) != 0};
        }
        return object;
    }

    std::uint32_t PPU::paletteColor(bool object, unsigned palette, unsigned color) const
    {
        if (!colorMode_)
        {
            const auto mapping = object ? (palette ? obp1_ : obp0_) : bgp_;
            const unsigned shade = (mapping >> (color * 2)) & 3;
            const unsigned level = 255 - shade * 85;
            return 0xFF000000 | (level << 16) | (level << 8) | level;
        }
        // CGB armazena RGB de cinco bits por canal em dois bytes little-endian.
        // A expansão replica bits para ocupar oito bits, sem simular a resposta do LCD.
        const auto &colors = object ? objColors_ : bgColors_;
        const unsigned offset = palette * 8 + color * 2;
        const unsigned rgb = colors[offset] | (colors[offset + 1] << 8);
        const auto expand = [](unsigned value)
        { return (value << 3) | (value >> 2); };
        return 0xFF000000 | (expand(rgb & 31) << 16) | (expand((rgb >> 5) & 31) << 8) | expand((rgb >> 10) & 31);
    }

    std::uint32_t PPU::mix(Pixel background, const ObjectPixel &object) const
    {
        if (!colorMode_ && !(lcdc_ & 1))
            background.color = 0;
        const bool bgWins = background.color != 0 && (colorMode_ ? (lcdc_ & 1) && (background.priority || object.behind)
                                                                 : object.behind);
        if ((lcdc_ & 2) && object.color != 0 && !bgWins)
            return paletteColor(true, object.palette, object.color);
        if (!colorMode_ && !(lcdc_ & 1))
            return 0xFFFFFFFF;
        return paletteColor(false, background.palette, background.color);
    }

    void PPU::transfer(const Memory::Memory &memory, IO::Interrupts &interrupts)
    {
        if (objectTicks_ != 0)
        {
            // No CGB físico a busca continua com OBJ off, inclusive em modo DMG.
            // O último dot já pertence à mesclagem e não pode ser cancelado.
            if (!colorHardware_ && !(lcdc_ & 2) && objectTicks_ > 1)
            {
                objectTicks_ = 0;
                objectWaiting_ = false;
                objectWait_ = 0;
            }
            else
            {
                fetchObject(memory);
                return;
            }
        }
        if (warmup_ <= 8 && discard_ == 0)
        {
            if (warmup_ == 0 && !windowActive_ && windowTriggered_ && (lcdc_ & 0x20) && (colorMode_ || (lcdc_ & 1)) &&
                wx_ <= 166 && (wx_ < 7 ? x_ == 0 : x_ + 7 == wx_))
            {
                windowActive_ = true;
                if (windowDrawn_)
                    ++windowLine_;
                windowPixel_ = wx_ < 7 ? 7 - wx_ : 0;
                bgHead_ = bgSize_ = fetchStep_ = fetchTicks_ = fetchX_ = 0;
                discard_ = wx_ < 7 ? 7 - wx_ : 0;
                windowDelay_ = 6;
            }
            if (windowDelay_ == 0 && startObject())
            {
                fetchObject(memory);
                return;
            }
        }
        if (warmup_ != 0)
        {
            if (warmup_ <= 8)
            {
                bgHead_ = (bgHead_ + 1) & 15;
                --bgSize_;
                fetchBackground(memory);
                if (discard_)
                {
                    --discard_;
                    return;
                }
                for (unsigned i = 0; i < 7; ++i)
                    objFIFO_[i] = objFIFO_[i + 1];
                objFIFO_[7] = ObjectPixel{};
            }
            --warmup_;
            return;
        }
        if (windowDelay_ != 0)
        {
            fetchBackground(memory);
            --windowDelay_;
            return;
        }
        if (bgSize_ == 0)
        {
            fetchBackground(memory);
            return;
        }
        const auto pixel = bgFIFO_[bgHead_];
        bgHead_ = (bgHead_ + 1) & 15;
        --bgSize_;
        if (discard_ != 0)
        {
            --discard_;
            fetchBackground(memory);
            return;
        }
        back_[line_ * Width + x_] = mix(pixel, withExtraSprites(memory, objFIFO_[0]));
        for (unsigned i = 0; i < 7; ++i)
            objFIFO_[i] = objFIFO_[i + 1];
        objFIFO_[7] = ObjectPixel{};
        if (windowActive_)
        {
            windowDrawn_ = true;
            windowPixel_ = (windowPixel_ + 1) & 7;
        }
        if (++x_ == Width)
        {
            mode_ = 0;
            updateSTAT(interrupts);
        }
        else if (!colorHardware_ && windowActive_ && !(lcdc_ & 0x20) && windowPixel_ == 0)
        {
            // DMG encerra o tile corrente da janela antes de retomar o BG.
            // O retorno é alinhado a tile, sem reaplicar o descarte fino de SCX.
            windowActive_ = false;
            bgHead_ = bgSize_ = fetchStep_ = fetchTicks_ = 0;
            fetchX_ = x_ / 8;
        }
        fetchBackground(memory);
    }

    void PPU::tick(const Memory::Memory &memory, IO::Interrupts &interrupts, bool dmaActive)
    {
        if (!enabled())
            return;
        const bool statWritePending = statWriteTicks_ != 0;
        if (statWritePending)
            --statWriteTicks_;
        ++dot_;
        if (lcdStartup_ && dot_ == 80)
        {
            mode_ = 3;
            beginTransfer();
            updateSTAT(interrupts);
        }
        else if (mode_ == 2)
        {
            scanOAM(memory, dmaActive);
            if (dot_ == 80)
            {
                mode_ = 3;
                beginTransfer();
                updateSTAT(interrupts);
            }
        }
        else if (mode_ == 3)
            transfer(memory, interrupts);

        if (dot_ == 456)
        {
            if (windowDrawn_)
                ++windowLine_;
            dot_ = 0;
            lcdStartup_ = false;
            if (++line_ == 154)
            {
                line_ = windowLine_ = 0;
                windowTriggered_ = false;
            }
            if (line_ == Height)
            {
                // No DMG, a seleção de modo 2 também sinaliza a entrada em
                // VBlank. Não há nova borda se outra fonte mantém STAT alto.
                if (!colorHardware_ && (stat_ & 0x20) && !statLine_)
                    interrupts.request(1);
                mode_ = 1;
                interrupts.request(0);
                // A publicação ocorre de uma vez em VBlank; o primeiro frame após
                // religar o LCD mantém a imagem branca preparada na escrita de LCDC.
                if (!blankFirstFrame_)
                    front_ = back_;
                blankFirstFrame_ = false;
                ++frameNumber_;
            }
            else if (line_ < Height)
                mode_ = 2;
            beginLine();
            updateSTAT(interrupts);
        }
        else if (line_ == 153 && dot_ == 4)
            updateSTAT(interrupts);
        if (statWritePending)
            updateSTAT(interrupts);
    }
}
