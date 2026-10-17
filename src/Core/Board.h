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

// Owns the emulated hardware. Component wiring and cycle scheduling come next.
class Board
{
public:
    explicit Board(Model model = Model::DMG);

    Model model() const;
    ExecutionMode executionMode() const;

private:
    Model model_;
    ExecutionMode executionMode_ = ExecutionMode::DMG;
    APU apu_;
    BUS bus_;
    CPU cpu_;
    Cartridge cartridge_;
    DMA dma_;
    Interrupts interrupts_;
    Joypad joypad_;
    Serial serial_;
    Timer timer_;
    Memory memory_;
    PPU ppu_;
};
