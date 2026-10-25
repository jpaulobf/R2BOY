#include "System/RewindBuffer.h"
#include "Core/Board.h"
#include <iostream>
#include <stdexcept>

using namespace R2BOY;
namespace
{
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void cadenceAndDuration()
    {
        using Buffer = System::RewindBuffer;
        for (unsigned interval : {10u, 5u, 1u})
        {
            Buffer buffer(interval);
            check(buffer.captureDue(0), "initial state is captured immediately");
            check(!buffer.popBefore(0), "empty history is safe");
            unsigned captures = 0;
            // Mais de duas voltas completas no ring buffer, com pequenas sobras de instrução.
            for (std::uint64_t frame = 0; frame <= 1500; ++frame)
            {
                const auto cycles = frame * Buffer::FrameCycles + 4;
                check(buffer.captureDue(cycles) == (frame % interval == 0),
                      "capture cadence follows emulated frames at every level");
                if (buffer.captureDue(cycles))
                {
                    buffer.push(cycles, {static_cast<std::uint8_t>(frame)});
                    ++captures;
                }
            }
            check(captures == 1500 / interval + 1, "all scheduled captures are recorded");
            const auto end = 1500 * Buffer::FrameCycles + 4;
            auto current = end;
            unsigned restored = 0;
            while (auto snapshot = buffer.popBefore(current))
            {
                check(snapshot->cycles < current, "rewind must always move backwards");
                check(current - snapshot->cycles == interval * Buffer::FrameCycles,
                      "wrapped ring restores snapshots in reverse order without gaps");
                check(end - snapshot->cycles <= Buffer::MaxCycles,
                      "history never exceeds ten seconds of emulated time");
                check(snapshot->state == std::vector<std::uint8_t>{
                                             static_cast<std::uint8_t>(snapshot->cycles / Buffer::FrameCycles)},
                      "wrapped slots retain the matching state bytes");
                current = snapshot->cycles;
                ++restored;
            }
            check(restored == Buffer::MaxCycles / (interval * Buffer::FrameCycles),
                  "ring retains all snapshots in the ten-second window");
            check(buffer.size() == 0, "exhausted history remains empty");
        }
    }

    void branchingAndReset()
    {
        using Buffer = System::RewindBuffer;
        Buffer buffer(5);
        for (unsigned frame : {0u, 5u, 10u, 15u})
            buffer.push(frame * Buffer::FrameCycles, {static_cast<std::uint8_t>(frame)});
        auto snapshot = buffer.popBefore(15 * Buffer::FrameCycles);
        check(snapshot && snapshot->cycles == 10 * Buffer::FrameCycles,
              "rewind skips the snapshot matching the current state");
        check(!buffer.captureDue(14 * Buffer::FrameCycles), "branch waits for its next capture");
        check(buffer.captureDue(15 * Buffer::FrameCycles), "branch resumes normal capture cadence");
        buffer.push(15 * Buffer::FrameCycles, {99});
        snapshot = buffer.popBefore(16 * Buffer::FrameCycles);
        check(snapshot && snapshot->state == std::vector<std::uint8_t>{99},
              "resuming replaces the abandoned future");
        buffer.clear();
        check(!buffer.popBefore(20 * Buffer::FrameCycles) && buffer.captureDue(0),
              "runtime reset discards history and restarts capture");
        buffer.push(0, {1});
        buffer.configure(1);
        check(buffer.size() == 0 && buffer.interval() == 1, "changing level discards old history");
        buffer.push(0, {1});
        check(!buffer.popBefore(Buffer::MaxCycles + 1), "stale history cannot rewind beyond ten seconds");
    }

    void restoresHardware()
    {
        for (auto model : {Core::Model::DMG, Core::Model::CGB})
        {
            Core::Board board(model);
            std::vector<std::uint8_t> rom(0x8000, 0);
            rom[0x143] = model == Core::Model::CGB ? 0x80 : 0;
            rom[0x100] = 0x18;
            rom[0x101] = 0xFE; // JR -2
            board.loadCartridge(std::move(rom), "rewind test");
            System::RewindBuffer buffer(1);
            std::vector<std::vector<std::uint8_t>> expected;
            std::vector<Common::Framebuffer> frames;
            for (unsigned frame = 0; frame < 8; ++frame)
            {
                board.write(0xC000, static_cast<std::uint8_t>(frame));
                while (board.baseClockCycles() < (frame + 1) * System::RewindBuffer::FrameCycles)
                    board.step();
                board.takeAudioSamples();
                expected.push_back(board.saveState());
                frames.push_back(board.framebuffer());
                buffer.push(board.baseClockCycles(), expected.back());
            }
            board.setAudioOutputVolume(0, 35);
            board.setUnlimitedSprites(true);
            for (int frame = 6; frame >= 0; --frame)
            {
                auto snapshot = buffer.popBefore(board.baseClockCycles());
                check(snapshot.has_value(), "hardware history contains previous frames");
                board.loadState(snapshot->state);
                check(board.saveState() == expected[frame], "rewind restores the complete hardware state");
                check(board.read(0xC000) == frame && board.framebuffer() == frames[frame],
                      "rewind restores memory and the published image");
                check(board.apu().outputVolume(0) == 35, "rewind preserves session audio preferences");
            }
            const auto before = board.saveState();
            while (board.baseClockCycles() < 2 * System::RewindBuffer::FrameCycles)
                board.step();
            const auto future = board.saveState();
            board.loadState(before);
            while (board.baseClockCycles() < 2 * System::RewindBuffer::FrameCycles)
                board.step();
            check(board.saveState() == future, "forward execution after rewind is deterministic");
        }
    }
}

int main()
{
    try
    {
        cadenceAndDuration();
        branchingAndReset();
        restoresHardware();
        std::cout << "Rewind buffer and state restoration tests passed.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}