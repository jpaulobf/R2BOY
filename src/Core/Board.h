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
     * É o ponto de composição do Core, independente de SDL e ImGui. As conexões
     * entre os dispositivos, o reset e a sincronização por ciclos ainda serão
     * implementados. Por enquanto, armazena o modelo e o cartucho carregado.
     */
    class Board
    {
    public:
        // Define o modelo físico; não inicia nem executa o hardware.
        explicit Board(Model model = Model::DMG);

        // Modelo físico escolhido na construção, independente da ROM carregada.
        Model model() const;

        // Modo de execução atual; permanece DMG até a implementação da seleção de modo.
        ExecutionMode executionMode() const;

        // Acesso ao cartucho pertencente à placa para realizar seu carregamento.
        Cartridge::Cartridge &cartridge() { return cartridge_; }

        // Consulta ao cartucho sem permitir alterações, utilizada pela interface.
        const Cartridge::Cartridge &cartridge() const { return cartridge_; }

    private:
        Model model_;
        ExecutionMode executionMode_ = ExecutionMode::DMG;
        APU::APU apu_;
        BUS::BUS bus_;
        CPU::CPU cpu_;
        Cartridge::Cartridge cartridge_;
        IO::DMA dma_;
        IO::Interrupts interrupts_;
        IO::Joypad joypad_;
        IO::Serial serial_;
        IO::Timer timer_;
        Memory::Memory memory_;
        PPU::PPU ppu_;
    };
}