#pragma once

#include "Model.h"
#include "APU/APU.h"
#include "BUS/BUS.h"
#include "CPU/CPU.h"
#include "Cartridge/Cartridge.h"
#include "IO/DMA.h"
#include "IO/Interrupts.h"
#include "IO/Joypad.h"
#include "IO/Serial.h"
#include "IO/Timer.h"
#include "Memory/Memory.h"
#include "PPU/PPU.h"
#include <algorithm>
#include <functional>
#include <utility>

namespace R2BOY::Core
{
    /**
     * Reúne e mantém vivos os componentes do hardware emulado.
     * É o ponto de composição do Core, independente de SDL e ImGui. Conecta o BUS
     * ao cartucho, às memórias e a IF/IE. A CPU chama o clock da Board a cada
     * M-cycle; a Board avança os periféricos em quatro passos de um T-cycle.
     */
    class Board
    {
    public:
        // Captura estados salvos entre instruções da CPU.
        std::vector<std::uint8_t> saveState();

        // Carrega um estado salvo previamente, restaurando o hardware para o ponto capturado.
        void loadState(const std::vector<std::uint8_t> &data);

        // Define o modelo físico; não inicia nem executa o hardware.
        explicit Board(Model model = Model::DMG);

        // O BUS mantém referências aos membros; a placa não pode ser copiada/movida.
        Board(const Board &) = delete;

        // Impede substituir componentes aos quais CPU e BUS já estão vinculados.
        Board &operator=(const Board &) = delete;

        // Carrega de forma transacional e reinicia a memória volátil no modo do jogo.
        // Rejeita jogos exclusivos CGB quando o modelo físico é DMG.
        void loadCartridge(std::vector<std::uint8_t> rom, std::string source);

        // Instala um cartucho já validado, inclusive com RAM restaurada pela plataforma.
        void loadCartridge(Cartridge::Cartridge cartridge);

        // Remove o cartucho e reinicia o hardware; a plataforma deve salvar a RAM antes.
        void unloadCartridge();

        // Apenas memória e bancos; ainda não executa reset da CPU/periféricos.
        void resetMemory();

        // Inicializa memória, CPU, IF/IE, joypad, serial, timer, DMA e PPU sem boot ROM.
        // A PPU começa em LY=0/modo 2; o mixer recebe NR50=77/NR51=F3, sem canais.
        void reset();

        // Executa uma instrução com hardware intercalado por M-cycle. Retorna
        // T-cycles consumidos (ou a unidade de espera STOP); sem cartucho retorna zero.
        unsigned step();

        // Clock efetivamente entregue ao hardware desde reset(); não inclui espera em STOP.
        std::uint64_t clockCycles() const { return clockCycles_; }

        // Tempo base de 4.194.304 Hz, independente da velocidade da CPU e do LCD.
        // Double speed e overclock dividem esse avanço por seus multiplicadores.
        std::uint64_t baseClockCycles() const { return baseClockCycles_; }
        bool doubleSpeed() const { return speedSwitch_.doubleSpeed(); }
        bool switchingSpeed() const { return speedSwitch_.switching(); }

        // Hack de 1x a 2x somente na CPU, independente de KEY1 e do modo DMG/CGB.
        // Preferência da sessão: preservada em reset, troca de ROM e load state.
        void setCPUOverclock(bool enabled) { cpuOverclockEnabled_ = enabled; }

        // Retorna se o overclock da CPU está habilitado.
        bool isCPUOverclockEnabled() const { return cpuOverclockEnabled_; }

        // Percentual da velocidade nativa (100 a 200); preferência da sessão.
        void setCPUOverclockPercent(int percent) { cpuOverclockPercent_ = std::clamp(percent, 100, 200); }
        int cpuOverclockPercent() const { return cpuOverclockPercent_; }

        // Retorna se o hack de sprites ilimitadas está habilitado.
        void setUnlimitedSprites(bool enabled) { ppu_.setUnlimitedSprites(enabled); }

        // Preferências de composição visual, independentes dos bits de LCDC.
        void setRenderLayers(bool tiles, bool sprites, bool window) { ppu_.setRenderLayers(tiles, sprites, window); }

        // Retorna se o hack de sprites ilimitadas está habilitado.
        bool isUnlimitedSpritesEnabled() const { return ppu_.isUnlimitedSpritesEnabled(); }

        // Multiplicador efetivo de velocidade da CPU, considerando double speed e overclock.
        double cpuSpeedMultiplier() const
        {
            return (doubleSpeed() ? 2.0 : 1.0) * (cpuOverclockEnabled_ ? cpuOverclockPercent_ / 100.0 : 1.0);
        }

        // Controle de diagnóstico: permite observar ciclos e acordar STOP explicitamente.
        CPU::CPU &cpu() { return cpu_; }

        // Acesso constante à CPU para diagnóstico/testes.
        const CPU::CPU &cpu() const { return cpu_; }

        // Observação por T-cycle, depois dos periféricos. Uma leitura de vídeo
        // pode já ter sido amostrada antes da borda da PPU; usar ReadObserver
        // para observar esse instante. Inclui ciclos sem dot de vídeo (double speed ou
        // overclock); STOP não gera eventos. Não executar step/reset/loadState
        // ou alterar hardware a partir do observador.
        struct Timing
        {
            std::uint64_t cpuCycles, baseCycles;
            PPU::PPU::Timing ppu;
            std::uint8_t requestedInterrupts, enabledInterrupts;
            bool dmaActive, doubleSpeed;
            bool vramDMAActive;
            unsigned vramDMABytes;
            bool vramDMABusy;
        };
        Timing timing() const
        {
            return {clockCycles_, baseClockCycles_, ppu_.timing(), interrupts_.readIF(),
                    interrupts_.readIE(), dma_.active(), doubleSpeed(), vramDMA_.active(), vramDMA_.remaining(), vramDMA_.cpuHeld()};
        }
        using TickObserver = std::function<void(const Timing &)>;
        // Diagnóstico da sessão, não serializado; preservado em reset/loadState.
        void setTickObserver(TickObserver observer) { tickObserver_ = std::move(observer); }
        // Dado e estado na amostragem real da leitura da CPU; diferente do
        // CycleObserver da CPU, chamado somente ao concluir o M-cycle.
        // Mesmas restrições de não modificar/reentrar no hardware do TickObserver.
        using ReadObserver = std::function<void(std::uint16_t, std::uint8_t, const Timing &)>;
        void setReadObserver(ReadObserver observer) { readObserver_ = std::move(observer); }

        // Saída da porta serial para diagnóstico/testes, sem dependência de console no Core.
        std::string takeSerialOutput() { return serial_.takeOutput(); }

        // Acesso constante à APU para diagnóstico/testes.
        const APU::APU &apu() const { return apu_; }

        // Amostras estéreo intercaladas (L, R em [-1, 1]) geradas pela APU desde a última chamada,
        // na taxa definida por setAudioSampleRate().
        std::vector<float> takeAudioSamples() { return apu_.takeSamples(); }

        // Configura a taxa de amostragem de áudio da APU.
        void setAudioSampleRate(std::uint32_t rate) { apu_.setSampleRate(rate); }

        // Configura os canais de saída de áudio da APU.
        void setAudioOutputChannels(std::uint8_t mask) { apu_.setOutputChannels(mask); }

        // Volume de escuta de cada canal, independente dos registradores emulados.
        void setAudioOutputVolume(unsigned channel, int percent) { apu_.setOutputVolume(channel, percent); }

        // Último frame completo publicado pela PPU, sem dependência da plataforma.
        const Common::Framebuffer &framebuffer() const { return ppu_.framebuffer(); }

        // Contador de entradas em VBlank da PPU; não representa frames apresentados pela janela.
        std::uint64_t frameNumber() const { return ppu_.frameNumber(); }

        // Estado combinado pela plataforma; cada bit em 1 representa um botão pressionado.
        void setButtons(std::uint8_t buttons);

        // Acesso pelo barramento da CPU, sujeito aos bloqueios de DMA e PPU.
        std::uint8_t read(std::uint16_t address) const { return bus_.read(address); }

        // Acesso pelo barramento da CPU, sujeito aos bloqueios de DMA e PPU.
        void write(std::uint16_t address, std::uint8_t value) { bus_.write(address, value); }

        // Modelo físico escolhido na construção, independente da ROM carregada.
        Model model() const;

        // Modo selecionado a partir do modelo físico e do flag CGB da ROM.
        ExecutionMode executionMode() const;

        // Acesso para persistência/estado. Para trocar a ROM e reiniciar a memória,
        // use loadCartridge() em vez de carregar diretamente neste objeto.
        Cartridge::Cartridge &cartridge() { return cartridge_; }

        // Consulta ao cartucho sem permitir alterações, utilizada pela interface.
        const Cartridge::Cartridge &cartridge() const { return cartridge_; }

    private:
        void state(StateArchive &archive);
        std::uint8_t tickMachineCycle(CPU::CPU::CycleType type, std::uint16_t address, std::uint8_t value);
        std::uint64_t clockCycles_ = 0;
        std::uint64_t baseClockCycles_ = 0;
        bool baseClockPhase_ = false;
        bool cpuOverclockEnabled_ = false;
        int cpuOverclockPercent_ = 200;
        TickObserver tickObserver_;
        ReadObserver readObserver_;
        Model model_;
        ExecutionMode executionMode_ = ExecutionMode::DMG;
        APU::APU apu_;
        bool sequencerBitHigh_ = false; // Última fase do bit de DIV que aciona o frame sequencer.
        Cartridge::Cartridge cartridge_;
        IO::DMA dma_;
        IO::VRAMDMA vramDMA_;
        IO::Interrupts interrupts_;
        IO::Joypad joypad_;
        std::uint8_t buttons_ = 0;
        IO::Serial serial_;
        IO::Timer timer_;
        IO::SpeedSwitch speedSwitch_;
        Memory::Memory memory_;
        PPU::PPU ppu_;
        BUS::BUS bus_;
        CPU::CPU cpu_;
    };
}