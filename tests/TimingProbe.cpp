#include "Core/Board.h"
#include "Common/RomFile.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    std::uint64_t number(const char *text)
    {
        const std::string input(text);
        if (input.empty() || input.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("Cycle bounds must be unsigned decimal integers.");
        return std::stoull(input);
    }
}

int main(int argc, char **argv)
{
    using namespace R2BOY;
    try
    {
        if (argc < 5 || argc > 6)
            throw std::runtime_error("Usage: timing_probe <rom.gb> <trace.csv> <first-T> <last-T> [dmg|cgb]\n"
                                     "Inclusive CPU clock bounds; tracing stops at the instruction containing last-T.");
        const auto first = number(argv[3]), last = number(argv[4]);
        if (first > last || last == 0 || last > std::numeric_limits<std::uint64_t>::max() - 24)
            throw std::runtime_error("Invalid cycle interval.");
        const std::string model = argc == 6 ? argv[5] : "dmg";
        if (model != "dmg" && model != "cgb")
            throw std::runtime_error("Model must be dmg or cgb.");
        const auto source = std::filesystem::u8path(argv[1]);
        const auto destination = std::filesystem::u8path(argv[2]);
        if (std::filesystem::exists(destination))
            throw std::runtime_error("Trace destination already exists; choose a new file.");
        Core::Board board(model == "dmg" ? Core::Model::DMG : Core::Model::CGB);
        board.setCPUOverclock(false);
        board.setUnlimitedSprites(false);
        board.loadCartridge(Common::RomFile::read(source), source.u8string());
        std::ofstream output(destination);
        if (!output)
            throw std::runtime_error("Cannot open trace destination.");
        output << "event,cpu_t,base_t,pc,address,value,line,ly,dot,mode,x,lcd,stat_line,if,ie,vram_blocked,oam_blocked,"
                  "fetch_stage,fetch_phase,bg_fifo,map_address,tile_address,obj_ticks,obj_wait,obj_waiting,window,window_delay,dma,double_speed,lcdc,scy,"
                  "warmup,discard,window_pixel,window_line,scx,wx,wy,wy_triggered,gdma,gdma_bytes,vram_dma_busy\n";
        const auto emit = [&](const char *event, const Core::Board::Timing &s, unsigned address, unsigned value)
        {
            if (s.cpuCycles < first || s.cpuCycles > last)
                return;
            const auto &p = s.ppu;
            output << event << ',' << s.cpuCycles << ',' << s.baseCycles << ',' << board.cpu().registers().pc
                   << ',' << address << ',' << value << ',' << p.line << ',' << p.ly << ',' << p.dot << ',' << p.mode
                   << ',' << p.x << ',' << p.lcdEnabled << ',' << p.statLine << ',' << unsigned(s.requestedInterrupts)
                   << ',' << unsigned(s.enabledInterrupts) << ',' << p.vramBlocked << ',' << p.oamBlocked
                   << ',' << p.fetchStage << ',' << p.fetchPhase << ',' << p.fifoSize << ',' << p.mapAddress
                   << ',' << p.tileAddress << ',' << p.objectTicks << ',' << p.objectWait << ',' << p.objectWaiting
                   << ',' << p.windowActive << ',' << p.windowDelay << ',' << s.dmaActive << ',' << s.doubleSpeed
                   << ',' << unsigned(p.lcdc) << ',' << unsigned(p.scy)
                   << ',' << p.warmup << ',' << p.discard << ',' << p.windowPixel << ',' << p.windowLine
                   << ',' << unsigned(p.scx) << ',' << unsigned(p.wx) << ',' << unsigned(p.wy) << ',' << p.windowTriggered
                   << ',' << s.vramDMAActive << ',' << s.vramDMABytes << ',' << s.vramDMABusy << '\n';
        };
        board.setTickObserver([&](const Core::Board::Timing &s)
                              { emit("tick", s, 0, 0); });
        board.setReadObserver([&](std::uint16_t address, std::uint8_t value, const Core::Board::Timing &s)
                              { emit("sample", s, address, value); });
        board.cpu().setCycleObserver([&](const Core::CPU::CPU::Cycle &c)
                                     {
            using Type = Core::CPU::CPU::CycleType;
            emit(c.type == Type::Read ? "read" : c.type == Type::Write ? "write" : "idle",
                 board.timing(), c.address, c.value); });
        while (board.clockCycles() < last)
        {
            const auto before = board.clockCycles();
            board.step();
            if (board.clockCycles() == before)
                throw std::runtime_error("CPU entered STOP before the end of the trace interval.");
            if (board.cpu().locked())
                throw std::runtime_error("CPU locked on an invalid opcode.");
        }
        output.close();
        if (!output)
            throw std::runtime_error("Failed to write trace.");
        std::cout << "Trace captured; all numbers are decimal. This records emulator behavior, not a hardware reference.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}