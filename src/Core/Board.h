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
     * ao cartucho e às memórias. CPU, periféricos e sincronização ainda são esqueletos.
     */
    class Board
    {
    public:
        // Define o modelo físico; não inicia nem executa o hardware.
        explicit Board(Model model = Model::DMG);
        // O BUS mantém referências aos membros; a placa não pode ser copiada/movida.
        Board(const Board &) = delete;
        Board &operator=(const Board &) = delete;

        // Carrega de forma transacional e reinicia a memória volátil no modo do jogo.
        // Rejeita jogos exclusivos CGB quando o modelo físico é DMG.
        void loadCartridge(std::vector<std::uint8_t> rom, std::string source);

        // Instala um cartucho já validado, inclusive com RAM restaurada pela plataforma.
        void loadCartridge(Cartridge::Cartridge cartridge);

        // Apenas memória e bancos; ainda não executa reset da CPU/periféricos.
        void resetMemory();
        
        // Acesso direto ao barramento, utilizado internamente pelos periféricos.
        std::uint8_t read(std::uint16_t address) const { return bus_.read(address); }
        
        // Acesso direto ao barramento, utilizado internamente pelos periféricos.
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
        Model model_;
        ExecutionMode executionMode_ = ExecutionMode::DMG;
        APU::APU apu_;
        CPU::CPU cpu_;
        Cartridge::Cartridge cartridge_;
        IO::DMA dma_;
        IO::Interrupts interrupts_;
        IO::Joypad joypad_;
        IO::Serial serial_;
        IO::Timer timer_;
        Memory::Memory memory_;
        BUS::BUS bus_;
        PPU::PPU ppu_;
    };
}