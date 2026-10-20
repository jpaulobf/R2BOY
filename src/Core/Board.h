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
        // A PPU começa deterministicamente em LY=0/modo 2; áudio ainda não tem perfil pós-boot.
        void reset();

        // Executa uma instrução com hardware intercalado por M-cycle. Retorna
        // T-cycles consumidos (ou a unidade de espera STOP); sem cartucho retorna zero.
        unsigned step();

        // Clock efetivamente entregue ao hardware desde reset(); não inclui espera em STOP.
        std::uint64_t clockCycles() const { return clockCycles_; }

        // Controle de diagnóstico: permite observar ciclos e acordar STOP explicitamente.
        CPU::CPU &cpu() { return cpu_; }

        // Acesso constante à CPU para diagnóstico/testes.
        const CPU::CPU &cpu() const { return cpu_; }

        // Saída da porta serial para diagnóstico/testes, sem dependência de console no Core.
        std::string takeSerialOutput() { return serial_.takeOutput(); }

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
        // Único ponto de avanço do hardware: não repetir estes ciclos depois de CPU::step().
        void tickMachineCycle();
        std::uint64_t clockCycles_ = 0;
        Model model_;
        ExecutionMode executionMode_ = ExecutionMode::DMG;
        APU::APU apu_;
        Cartridge::Cartridge cartridge_;
        IO::DMA dma_;
        IO::Interrupts interrupts_;
        IO::Joypad joypad_;
        std::uint8_t buttons_ = 0;
        IO::Serial serial_;
        IO::Timer timer_;
        Memory::Memory memory_;
        PPU::PPU ppu_;
        BUS::BUS bus_;
        CPU::CPU cpu_;
    };
}