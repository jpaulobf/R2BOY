#pragma once

#include "Core/StateArchive.h"
#include <array>
#include <cstdint>
#include "Common/Framebuffer.h"
#include "Core/Memory/Memory.h"
#include "Core/IO/Interrupts.h"

namespace R2BOY::Core::PPU
{
    /**
     * PPU por dot: seleção de OAM, fetcher de tiles e FIFOs BG/OBJ independentes.
     * Mistura um pixel por dot quando o fetcher não está parado. Sem SDL no Core.
     * tick() recebe dots de vídeo, não T-cycles de CPU em double speed.
     * Ver tests/PPUTimingValidation.md para a revisão de modos/linhas e seus limites.
     */
    class PPU
    {
    public:
        static constexpr unsigned Width = Common::Constants::ScreenWidth;
        static constexpr unsigned Height = Common::Constants::ScreenHeight;

        // Reinicia registradores, FIFOs e frames; separa o modelo físico do modo do jogo.
        // afterBoot seleciona LCD ligado em modo 2; caso contrário, começa desligado.
        void reset(bool colorHardware = false, bool colorMode = false, bool afterBoot = true);

        // Avança um dot; dmaActive impede a seleção de sprites durante a varredura de OAM.
        void tick(const Memory::Memory &memory, IO::Interrupts &interrupts, bool dmaActive = false);

        // Consulta registradores LCD/paletas; endereços e recursos indisponíveis retornam 0xFF.
        std::uint8_t read(std::uint16_t address) const;

        // Atualiza registradores e reavalia STAT; LCD off congela o comparador LY/LYC.
        void write(std::uint16_t address, std::uint8_t value, IO::Interrupts &interrupts);

        // Perfil DMG: campos de endereçamento visíveis durante a fase de dados
        // da escrita da CPU (T2), antes de seu término. BGP/OBP usam latches
        // após a borda de vídeo em T2 (old|new) e T3 (new). Alimentação e demais
        // controles de LCDC são aplicados no fim do M-cycle. O CGB usa a fase
        // posterior; revisões anteriores ao CGB-D continuam fora do perfil.
        std::uint8_t earlyWriteMask(std::uint16_t address, std::uint8_t value) const
        {
            if (colorHardware_)
                return 0;
            if (address == 0xFF42 || address == 0xFF43)
                return 0xFF;
            if (address >= 0xFF47 && address <= 0xFF49)
                return 0xFF;
            if (address >= 0xFF47 && address <= 0xFF49)
                return 0xFF;
            if (address == 0xFF40 && ((lcdc_ ^ value) & 0x80) == 0)
                return 0x58; // BG map, tile data select e window map.
            return 0;
        }

        // Estado do bit de habilitação do LCD em LCDC.
        bool enabled() const { return (lcdc_ & 0x80) != 0; }

        // Modo atual: 0=HBlank, 1=VBlank, 2=OAM, 3=transferência de pixels.
        unsigned mode() const { return mode_; }

        // The initial mode-0 interval after LCD on is warmup, not HBlank.
        bool vramDMAHBlank() const { return !enabled() || (mode_ == 0 && dot_ >= 80); }

        // Posição temporal dentro da linha, em dots, independente do pixel visível.
        unsigned dot() const { return dot_; }

        // Instantâneo de diagnóstico, após o último dot executado. Os campos do
        // fetcher só descrevem uma busca ativa em modo 3; phase=0 aguarda T1 e
        // phase=1 já selecionou o endereço e aguarda T2 (stage=3 aguarda push).
        struct Timing
        {
            unsigned line, ly, dot, mode, x;
            unsigned fetchStage, fetchPhase, fifoSize;
            unsigned objectTicks, objectWait, windowDelay;
            std::uint16_t mapAddress, tileAddress;
            bool lcdEnabled, statLine, vramBlocked, oamBlocked;
            bool objectWaiting, windowActive;
            std::uint8_t lcdc, scy;
            unsigned warmup, discard, windowPixel, windowLine;
            std::uint8_t scx, wx, wy;
            bool windowTriggered;
        };
        Timing timing() const
        {
            return {line_, ly(), dot_, mode_, x_, fetchStep_, fetchTicks_, bgSize_,
                    objectTicks_, objectWait_, windowDelay_, tileMapAddress_, tileAddress_,
                    enabled(), statLine_, blocksVRAM(), blocksOAM(), objectWaiting_, windowActive_, lcdc_, scy_,
                    warmup_, discard_, windowPixel_, windowLine_, scx_, wx_, wy_, windowTriggered_};
        }

        // A CPU não acessa VRAM durante a transferência de pixels com LCD ligado.
        bool blocksVRAM() const { return enabled() && mode_ == 3; }

        // A CPU não acessa OAM durante a seleção de sprites nem a transferência de pixels.
        bool blocksOAM() const { return enabled() && (mode_ == 2 || mode_ == 3); }

        // O frame publicado só muda em VBlank ou ao desligar LCD.
        const Common::Framebuffer &framebuffer() const { return front_; }

        // Conta entradas em VBlank desde reset(), inclusive o primeiro frame ocultado.
        std::uint64_t frameNumber() const { return frameNumber_; }

        // Inspeção de paleta sem acessar registradores ou alterar seus índices.
        std::uint32_t debugPaletteColor(bool object, unsigned palette, unsigned color) const
        {
            return paletteColor(object, palette, color);
        }

        // Habilita ou desabilita o hack de sprites ilimitadas.
        void setUnlimitedSprites(bool enabled) { unlimitedSprites_ = enabled; }

        // Retorna se o hack de sprites ilimitadas está habilitado.
        bool isUnlimitedSpritesEnabled() const { return unlimitedSprites_; }

        // Preferências visuais da sessão; preservam o pipeline e os registradores LCD.
        void setRenderLayers(bool tiles, bool sprites, bool window)
        {
            renderTiles_ = tiles;
            renderSprites_ = sprites;
            renderWindow_ = window;
        }

    private:
        friend class R2BOY::Core::StateArchive;
        void state(StateArchive &archive)
        {
            archive(front_, back_, bgColors_, objColors_, bgFIFO_, objFIFO_, sprites_);
            archive(fetchingObject_, lcdc_, stat_, scy_, scx_, lyc_, bgp_);
            archive(obp0_, obp1_, wy_, wx_, bgIndex_, objIndex_, opri_);
            archive(tile_, attributes_, tileY_, low_, high_, objectLow_, objectHigh_);
            archive(tileMapAddress_, tileAddress_, objectAddress_, line_, dot_, mode_, x_, spriteCount_);
            archive(nextSprite_, objectWaiting_, objectWait_, objectTicks_, bgHead_, bgSize_, fetchStep_, fetchTicks_, fetchX_);
            archive(warmup_, discard_, windowDelay_, windowLine_, frameNumber_, colorHardware_);
            archive(colorMode_, statLine_, windowTriggered_, windowActive_, windowDrawn_, blankFirstFrame_);
            archive(extraSprites_, extraSpriteCount_);
            archive(coincidence_, lcdStartup_, windowPixel_, statWriteTicks_);
            archive(windowInsertionBlocked_);
            if (archive.reading() && (line_ > 153 || dot_ > 455 || mode_ > 3 || x_ > Width ||
                                      spriteCount_ > 10 || nextSprite_ > spriteCount_ || bgHead_ > 15 || bgSize_ > 8 ||
                                      fetchStep_ > 3 || fetchTicks_ > 1 || objectTicks_ > 6 || objectWait_ > 5 ||
                                      (objectWaiting_ && objectTicks_ != 6) || (objectWait_ && !objectWaiting_) ||
                                      warmup_ > 12 || discard_ > 7 || windowDelay_ > 6 ||
                                      extraSpriteCount_ > extraSprites_.size() ||
                                      (lcdStartup_ && line_ != 0) || windowPixel_ > 7 || statWriteTicks_ > 4 ||
                                      (statWriteTicks_ && (colorHardware_ || !enabled()))))
                throw std::runtime_error("Invalid PPU save state.");
        }
        // Pixel de fundo/window antes da aplicação da paleta e da prioridade com OBJ.
        struct Pixel
        {
            std::uint8_t color = 0, palette = 0, priority = 0;
            void state(StateArchive &archive) { archive(color, palette, priority); }
        };
        // Pixel de sprite com os dados necessários para desempatar sobreposições.
        struct ObjectPixel
        {
            std::uint8_t color = 0, palette = 0, index = 0xFF, x = 0xFF;
            bool behind = false;
            void state(StateArchive &archive) { archive(color, palette, index, x, behind); }
        };
        // Entrada selecionada de OAM; index preserva a ordem original para prioridade.
        struct Sprite
        {
            std::uint8_t y = 0, x = 0, tile = 0, attributes = 0, index = 0;
            void state(StateArchive &archive) { archive(y, x, tile, attributes, index); }
        };

        // Compõe os sprites extras sem inserir esperas no pipeline nativo de dez objetos.
        ObjectPixel withExtraSprites(const Memory::Memory &memory, ObjectPixel object) const;

        // Valor visível de LY, incluindo a transição para zero no dot 4 da linha 153.
        std::uint8_t ly() const;
        // Combina as fontes de STAT e pede interrupção apenas na borda de subida.
        void updateSTAT(IO::Interrupts &interrupts);
        // Reinicia a seleção de sprites e verifica o disparo vertical da window.
        void beginLine();
        // Prepara FIFOs, descarte de SCX e ordem de busca dos sprites para o modo 3.
        void beginTransfer();
        // Examina uma entrada a cada dois dots; separa os dez nativos dos extras do hack.
        void scanOAM(const Memory::Memory &memory, bool dmaActive);
        // Avança o pipeline de um dot e entra em HBlank após o último pixel visível.
        void transfer(const Memory::Memory &memory, IO::Interrupts &interrupts);
        // Avança a busca de tile/atributos/planos do fundo ou da window na VRAM.
        void fetchBackground(const Memory::Memory &memory);
        // Enfileira oito pixels do tile somente quando o FIFO está vazio.
        bool pushBackground();
        // Inicia a busca de um sprite que alcançou a posição atual, se houver.
        bool startObject();
        // Avança BG até a liberação, busca o sprite em seis dots e mescla ao concluir.
        void fetchObject(const Memory::Memory &memory);
        // Insere pixels não transparentes no FIFO OBJ conforme a prioridade DMG/CGB.
        void mergeObject();
        // Resolve a prioridade entre fundo e sprite e devolve o pixel 0xAARRGGBB.
        std::uint32_t mix(Pixel background, const ObjectPixel &object) const;
        // Converte o índice de cor pela paleta DMG ou pela memória de cores CGB.
        std::uint32_t paletteColor(bool object, unsigned palette, unsigned color) const;

        // front_ é o frame publicado; back_ recebe os pixels da linha em construção.
        Common::Framebuffer front_{}, back_{};
        std::array<std::uint8_t, 64> bgColors_{}, objColors_{};
        std::array<Pixel, 16> bgFIFO_{};
        std::array<ObjectPixel, 8> objFIFO_{};
        std::array<Sprite, 10> sprites_{};
        std::array<Sprite, 30> extraSprites_{};
        unsigned extraSpriteCount_ = 0;
        bool unlimitedSprites_ = true;
        bool renderTiles_ = true, renderSprites_ = true, renderWindow_ = true;
        Sprite fetchingObject_{};
        std::uint8_t lcdc_ = 0, stat_ = 0, scy_ = 0, scx_ = 0, lyc_ = 0;
        std::uint8_t bgp_ = 0xFC, obp0_ = 0xFF, obp1_ = 0xFF, wy_ = 0, wx_ = 0;
        std::uint8_t bgIndex_ = 0, objIndex_ = 0, opri_ = 0;
        std::uint8_t tile_ = 0, attributes_ = 0, tileY_ = 0, low_ = 0, high_ = 0;
        std::uint8_t objectLow_ = 0, objectHigh_ = 0;
        std::uint16_t tileMapAddress_ = 0, tileAddress_ = 0, objectAddress_ = 0;
        unsigned line_ = 0, dot_ = 0, mode_ = 0, x_ = 0;
        unsigned spriteCount_ = 0, nextSprite_ = 0, objectTicks_ = 0;
        bool objectWaiting_ = false;
        unsigned objectWait_ = 0;
        unsigned bgHead_ = 0, bgSize_ = 0, fetchStep_ = 0, fetchTicks_ = 0, fetchX_ = 0;
        // Em warmup=8, discard conta a posição do comparador fino (módulo 8).
        // Após a partida, volta a ser o descarte restante de WX<7.
        unsigned warmup_ = 0, discard_ = 0, windowDelay_ = 0, windowLine_ = 0;
        unsigned windowPixel_ = 0;
        unsigned statWriteTicks_ = 0; // Pulso DMG: seleções de STAT ativas por um M-cycle.
        std::uint64_t frameNumber_ = 0;
        bool colorHardware_ = false, colorMode_ = false, statLine_ = false;
        bool windowTriggered_ = false, windowActive_ = false, windowDrawn_ = false;
        bool windowInsertionBlocked_ = false;
        bool blankFirstFrame_ = false;
        bool coincidence_ = true;
        bool lcdStartup_ = false;
    };
}