#include "Core/Board.h"
#include <algorithm>
#include <cmath>
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

    std::vector<std::uint8_t> rom()
    {
        std::vector<std::uint8_t> data(0x8000);
        // Fade NR50 from FF to 88, then mark completion in WRAM. Mirrors the
        // read/subtract/write wait used by Animaniacs without proprietary data.
        const std::uint8_t program[]{
            0x3E, 0xFF, 0xE0, 0x24, 0xF0, 0x24, 0xFE, 0x88,
            0x28, 0x06, 0xD6, 0x11, 0xE0, 0x24, 0x18, 0xF4,
            0x3E, 0x01, 0xEA, 0x00, 0xC0, 0x18, 0xFE};
        std::copy(std::begin(program), std::end(program), data.begin() + 0x100);
        return data;
    }

    void controls(Core::Model model)
    {
        Core::Board board(model);
        board.loadCartridge(rom(), "audio controls test");
        check(board.read(0xFF24) == 0x77 && board.read(0xFF25) == 0xF3, "post-boot mixer profile");
        for (unsigned i = 0; i < 256; ++i)
        {
            board.write(0xFF24, i);
            board.write(0xFF25, 255 - i);
            check(board.read(0xFF24) == i && board.read(0xFF25) == 255 - i, "mixer readback including VIN bits");
        }
        board.write(0xFF26, 0x7F);
        check(board.read(0xFF26) == 0x70, "NR52 writable power bit and unused bits");
        board.write(0xFF24, 0xFF);
        board.write(0xFF25, 0xFF);
        check(board.read(0xFF24) == 0 && board.read(0xFF25) == 0, "power-off clears and locks mixer controls");
        const auto off = board.saveState();
        board.write(0xFF26, 0x80);
        check(board.read(0xFF24) == 0 && board.read(0xFF25) == 0, "power-on preserves cleared controls");
        board.write(0xFF24, 0x23);
        board.write(0xFF25, 0xA5);
        const auto on = board.saveState();
        board.reset();
        board.loadState(on);
        check(board.read(0xFF24) == 0x23 && board.read(0xFF25) == 0xA5, "restore active mixer controls");
        board.loadState(off);
        board.write(0xFF24, 0x77);
        check(board.read(0xFF26) == 0x70 && board.read(0xFF24) == 0, "restore power-off write lock");
        board.reset();
        check(board.read(0xFF24) == 0x77 && board.read(0xFF25) == 0xF3, "reset mixer controls");
        for (unsigned i = 0; i < 100; ++i)
            board.step();
        check(board.read(0xC000) == 1 && board.read(0xFF24) == 0x88, "CPU volume fade must finish");
        board.write(0xFF46, 0xC0);
        for (unsigned i = 0; i < 3; ++i)
            board.step();
        board.write(0xFF24, 0x11);
        board.write(0xFF25, 0x22);
        check(board.read(0xFF24) == 0x11 && board.read(0xFF25) == 0x22,
              "mixer I/O remains accessible during DMA");
        board.write(0xFF26, 0);
        check(board.read(0xFF24) == 0 && board.read(0xFF25) == 0 && board.read(0xFF26) == 0x70,
              "power control remains accessible during DMA");
    }

    // Mantém a CPU girando e avança ao menos o número de ciclos base indicado.
    void run(Core::Board &board, std::uint64_t cycles)
    {
        const auto target = board.baseClockCycles() + cycles;
        while (board.baseClockCycles() < target)
            board.step();
    }

    void idle(Core::Board &board)
    {
        board.loadCartridge(rom(), "apu test");
        run(board, 2000); // termina o programa de fade e entra no laço ocioso
        board.write(0xFF24, 0x77);
        board.write(0xFF25, 0xFF);
    }

    // DIV zerado alinha o frame sequencer ao passo 0 para temporizações exatas.
    void alignSequencer(Core::Board &board)
    {
        board.write(0xFF26, 0x00);
        board.write(0xFF26, 0x80);
        board.write(0xFF04, 0);
    }

    constexpr unsigned stepCycles = 8192; // um passo do frame sequencer (512 Hz)

    // Valores da tabela de leitura da Pan Docs (bits não usados lidos como 1).
    void registerMasks()
    {
        Core::Board board(Core::Model::CGB);
        idle(board);
        board.write(0xFF26, 0x00);
        board.write(0xFF26, 0x80);
        const std::pair<std::uint16_t, std::uint8_t> expected[] = {
            {0xFF10, 0x80}, {0xFF11, 0x3F}, {0xFF12, 0x00}, {0xFF13, 0xFF}, {0xFF14, 0xBF}, {0xFF15, 0xFF}, {0xFF16, 0x3F}, {0xFF17, 0x00}, {0xFF18, 0xFF}, {0xFF19, 0xBF}, {0xFF1A, 0x7F}, {0xFF1B, 0xFF}, {0xFF1C, 0x9F}, {0xFF1D, 0xFF}, {0xFF1E, 0xBF}, {0xFF1F, 0xFF}, {0xFF20, 0xFF}, {0xFF21, 0x00}, {0xFF22, 0x00}, {0xFF23, 0xBF}, {0xFF24, 0x00}, {0xFF25, 0x00}, {0xFF26, 0xF0 & 0xF0}, {0xFF27, 0xFF}, {0xFF2F, 0xFF}};
        for (const auto &[address, value] : expected)
            check(board.read(address) == (address == 0xFF26 ? 0xF0 : value), "register read mask");
        // Registradores escrevíveis devolvem o valor escrito, com os bits write-only em 1.
        board.write(0xFF11, 0xC5);
        board.write(0xFF12, 0xA3);
        board.write(0xFF13, 0x12);
        board.write(0xFF1C, 0x60);
        board.write(0xFF22, 0x5B);
        check(board.read(0xFF11) == 0xFF && board.read(0xFF12) == 0xA3 && board.read(0xFF13) == 0xFF,
              "NR11/NR12/NR13 readback");
        check(board.read(0xFF1C) == 0xFF && board.read(0xFF22) == 0x5B, "NR32/NR43 readback");
        board.write(0xFF1A, 0x80);
        check(board.read(0xFF1A) == 0xFF, "NR30 DAC bit");
    }

    void lengthCounter()
    {
        Core::Board board(Core::Model::CGB);
        idle(board);
        alignSequencer(board);
        // Length 1: o canal 2 desliga no primeiro clock de length (passo 0 ao completar 8192 ciclos).
        board.write(0xFF16, 0x3F);
        board.write(0xFF17, 0xF0);
        board.write(0xFF19, 0xC0);
        check(board.read(0xFF26) & 0x02, "channel 2 active after trigger");
        run(board, stepCycles * 3);
        check(!(board.read(0xFF26) & 0x02), "length counter disables channel 2");
        // Sem length habilitado o canal continua ativo por tempo indeterminado.
        board.write(0xFF16, 0x3F);
        board.write(0xFF19, 0x80);
        run(board, stepCycles * 16);
        check(board.read(0xFF26) & 0x02, "channel without length enable keeps playing");
        // Canal de onda: length de 8 bits (256 - n).
        alignSequencer(board);
        board.write(0xFF1A, 0x80);
        board.write(0xFF1B, 0xFF);
        board.write(0xFF1E, 0xC0);
        check(board.read(0xFF26) & 0x04, "channel 3 active after trigger");
        run(board, stepCycles * 3);
        check(!(board.read(0xFF26) & 0x04), "wave length counter disables channel 3");
        // Trigger com length zerado recarrega 64/256: 64 * 16384 ciclos até desligar.
        board.write(0xFF20, 0x00);
        board.write(0xFF21, 0xF0);
        board.write(0xFF23, 0xC0);
        run(board, 16384 * 62);
        check(board.read(0xFF26) & 0x08, "noise length reload to 64");
        run(board, 16384 * 4);
        check(!(board.read(0xFF26) & 0x08), "noise length expires after 64 clocks");
    }

    void dacAndEnvelope()
    {
        Core::Board board(Core::Model::CGB);
        idle(board);
        // DAC desligado (NRx2 & 0xF8 == 0) impede o trigger e desliga o canal.
        board.write(0xFF12, 0x00);
        board.write(0xFF14, 0x80);
        check(!(board.read(0xFF26) & 1), "trigger with DAC off");
        board.write(0xFF12, 0xF0);
        board.write(0xFF14, 0x80);
        check(board.read(0xFF26) & 1, "trigger with DAC on");
        board.write(0xFF12, 0x00);
        check(!(board.read(0xFF26) & 1), "disabling DAC disables channel");
        board.write(0xFF1A, 0x00);
        board.write(0xFF1E, 0x80);
        check(!(board.read(0xFF26) & 4), "wave trigger with DAC off");

        // Envelope decrescente, período 1: um passo a cada 8 passos do sequenciador (65536 ciclos).
        alignSequencer(board);
        board.write(0xFF17, 0xF1);
        board.write(0xFF19, 0x80);
        check(board.apu().channelVolume(1) == 15, "initial envelope volume");
        run(board, stepCycles * 8);
        check(board.apu().channelVolume(1) == 14, "envelope decrements once per 64 Hz clock");
        run(board, stepCycles * 8 * 3);
        check(board.apu().channelVolume(1) == 11, "envelope keeps decrementing");
        // Crescente, período 2, saturando em 15.
        board.write(0xFF22, 0x00);
        board.write(0xFF21, 0x0A);
        board.write(0xFF21, 0x9A);
        board.write(0xFF23, 0x80);
        check(board.apu().channelVolume(3) == 9, "noise initial volume");
        run(board, stepCycles * 8 * 14);
        check(board.apu().channelVolume(3) == 15, "increasing envelope saturates at 15");
        // Período 0 mantém o volume.
        board.write(0xFF17, 0x80);
        board.write(0xFF19, 0x80);
        run(board, stepCycles * 24);
        check(board.apu().channelVolume(1) == 8, "envelope period 0 holds volume");
    }

    void sweep()
    {
        Core::Board board(Core::Model::CGB);
        idle(board);
        alignSequencer(board);
        // Pace 1, soma, shift 1: f' = f + f/2 no clock de sweep (passo 2).
        board.write(0xFF10, 0x11);
        board.write(0xFF12, 0xF0);
        board.write(0xFF13, 0x00);
        board.write(0xFF14, 0x84); // f = 0x400
        check(board.apu().channelFrequency(0) == 0x400 && (board.read(0xFF26) & 1), "sweep trigger keeps frequency");
        run(board, stepCycles * 4);
        check(board.apu().channelFrequency(0) == 0x600, "sweep adds frequency/2");
        run(board, stepCycles * 4);
        check(board.apu().channelFrequency(0) == 0x600 + 0x300 - 0x100 || board.apu().channelFrequency(0) == 0x900 || !(board.read(0xFF26) & 1),
              "sweep second iteration");
        // Overflow acima de 2047 desliga o canal imediatamente no trigger.
        board.write(0xFF10, 0x11);
        board.write(0xFF13, 0x00);
        board.write(0xFF14, 0x87); // f = 0x700
        check(!(board.read(0xFF26) & 1), "sweep overflow disables channel 1 at trigger");
        // Subtração: f' = f - f/2^n.
        board.write(0xFF10, 0x19);
        board.write(0xFF12, 0xF0);
        board.write(0xFF13, 0x00);
        board.write(0xFF14, 0x84);
        run(board, stepCycles * 4);
        check(board.apu().channelFrequency(0) == 0x200 && (board.read(0xFF26) & 1), "sweep subtracts frequency/2");
        // Trocar de subtração para soma após um cálculo negativo desliga o canal.
        board.write(0xFF10, 0x11);
        check(!(board.read(0xFF26) & 1), "negate-to-add switch disables channel 1");
    }

    void waveRam()
    {
        Core::Board board(Core::Model::CGB);
        idle(board);
        board.write(0xFF26, 0x00);
        board.write(0xFF26, 0x80);
        for (unsigned i = 0; i < 16; ++i)
            board.write(0xFF30 + i, static_cast<std::uint8_t>(i * 17));
        for (unsigned i = 0; i < 16; ++i)
            check(board.read(0xFF30 + i) == i * 17, "wave RAM readback while channel 3 is off");
        // Wave RAM sobrevive ao desligamento da APU e continua acessível.
        board.write(0xFF26, 0x00);
        check(board.read(0xFF31) == 17, "wave RAM preserved by power off");
        board.write(0xFF3F, 0xAB);
        check(board.read(0xFF3F) == 0xAB, "wave RAM writable while APU is off");
    }

    // Gera amostras estéreo e retorna os extremos de cada lado.
    struct Range
    {
        float lo = 1, hi = -1, loR = 1, hiR = -1;
    };

    Range capture(Core::Board &board, std::uint64_t cycles)
    {
        board.takeAudioSamples();
        run(board, cycles);
        const auto samples = board.takeAudioSamples();
        check(samples.size() % 2 == 0, "stereo interleaving");
        Range r;
        for (std::size_t i = 0; i + 1 < samples.size(); i += 2)
        {
            r.lo = std::min(r.lo, samples[i]);
            r.hi = std::max(r.hi, samples[i]);
            r.loR = std::min(r.loR, samples[i + 1]);
            r.hiR = std::max(r.hiR, samples[i + 1]);
            check(std::abs(samples[i]) <= 1.0f && std::abs(samples[i + 1]) <= 1.0f, "samples stay in range");
        }
        return r;
    }

    void synthesis()
    {
        Core::Board board(Core::Model::CGB);
        idle(board);
        board.setAudioSampleRate(48000);
        board.takeAudioSamples();
        run(board, 419430);
        const auto count = board.takeAudioSamples().size() / 2;
        check(count >= 4795 && count <= 4805, "sample count matches the configured rate");

        // Silêncio com todos os canais desligados.
        auto silent = capture(board, 100000);
        check(silent.hi - silent.lo < 0.01f && silent.hiR - silent.loR < 0.01f, "silence without channels");

        // Pulso 50% em ~1 kHz: onda quadrada audível.
        board.write(0xFF11, 0x80);
        board.write(0xFF12, 0xF0);
        board.write(0xFF13, 0x00);
        board.write(0xFF14, 0x86); // f = 0x600 => 131072/(2048-1536) = 256 Hz? (período 512*4*8)
        auto pulse = capture(board, 200000);
        check(pulse.hi - pulse.lo > 0.2f && pulse.hiR - pulse.loR > 0.2f, "pulse channel produces a waveform");
        // Duty 12,5% tem menos tempo em nível alto que 75%.
        const auto high = [&](std::uint8_t duty)
        {
            board.write(0xFF11, static_cast<std::uint8_t>(duty << 6));
            board.write(0xFF14, 0x86);
            board.takeAudioSamples();
            run(board, 8 * 512 * 4 * 8);
            const auto samples = board.takeAudioSamples();
            std::size_t n = 0;
            for (std::size_t i = 0; i < samples.size(); i += 2)
                n += samples[i] < -0.1f ? 1 : 0; // DAC invertido: volume alto é tensão negativa
            return n;
        };
        const auto d0 = high(0), d3 = high(3);
        check(d0 < d3, "duty 12.5% spends less time high than 75%");

        // Panning: apenas canal 1 na esquerda.
        board.write(0xFF25, 0x10);
        board.write(0xFF11, 0x80);
        board.write(0xFF14, 0x86);
        run(board, 1000000); // deixa o capacitor de saída assentar
        auto left = capture(board, 100000);
        check(left.hi - left.lo > 0.1f && left.hiR - left.loR < 0.01f, "NR51 routes channel 1 to the left only");
        // Volume mestre zero (1/8) reduz a amplitude.
        board.write(0xFF25, 0xFF);
        board.write(0xFF24, 0x77);
        board.write(0xFF14, 0x86);
        const auto loud = capture(board, 100000);
        board.write(0xFF24, 0x00);
        board.write(0xFF14, 0x86);
        const auto quiet = capture(board, 100000);
        check(quiet.hi - quiet.lo < (loud.hi - loud.lo) / 4, "NR50 master volume scales the output");
        board.write(0xFF24, 0x77);
        board.write(0xFF12, 0x00);

        // Canal de onda: forma de rampa nos 32 nibbles.
        for (unsigned i = 0; i < 16; ++i)
            board.write(0xFF30 + i, static_cast<std::uint8_t>(((2 * i) << 4) | (2 * i + 1)));
        board.write(0xFF1A, 0x80);
        board.write(0xFF1C, 0x20);
        board.write(0xFF1D, 0x00);
        board.write(0xFF1E, 0x87);
        auto wave = capture(board, 200000);
        check(wave.hi - wave.lo > 0.3f, "wave channel plays Wave RAM");
        board.write(0xFF1C, 0x00); // nível 0: mudo
        run(board, 1000000);
        auto mute = capture(board, 100000);
        check(mute.hi - mute.lo < 0.05f, "wave output level 0 mutes the channel");
        board.write(0xFF1A, 0x00);

        // Ruído: LFSR de 15 e 7 bits geram sinal variável.
        for (std::uint8_t mode : {0x00, 0x08})
        {
            board.write(0xFF21, 0xF0);
            board.write(0xFF22, mode);
            board.write(0xFF23, 0x80);
            auto noise = capture(board, 200000);
            check(noise.hi - noise.lo > 0.2f, "noise channel produces a signal");
        }
    }

    void triggerEdges()
    {
        using Core::APU::APU;
        // Observa cada T-cycle, sem a média de várias posições do duty.
        for (unsigned channel = 0; channel < 2; ++channel)
        {
            APU apu;
            apu.reset();
            apu.setSampleRate(APU::ClockRate);
            apu.write(0xFF25, channel == 0 ? 0x11 : 0x22);
            for (unsigned i = 0; i < 10000; ++i)
                apu.tick(); // O duty deve continuar parado antes do primeiro trigger.
            apu.takeSamples();
            const auto base = static_cast<std::uint16_t>(0xFF11 + channel * 5);
            apu.write(base, 0); // duty 12,5%: posição 0 alta, demais baixas
            apu.write(base + 1, 0xF0);
            apu.write(base + 2, 0xFE);
            apu.write(base + 3, 0x87); // período de oito T-cycles
            for (unsigned i = 0; i < 8; ++i)
                apu.tick();
            const auto first = apu.takeSamples();
            check(std::all_of(first.begin(), first.end(), [](float s)
                              { return s > 0; }),
                  "first pulse duty step is digital zero after waiting before trigger");
            for (unsigned i = 0; i < 56; ++i)
                apu.tick();
            check(apu.takeSamples().back() < 0, "pulse duty reaches position zero after exactly eight periods");
            apu.write(base + 3, 0x87);
            apu.tick();
            check(apu.takeSamples().back() < 0, "retrigger preserves pulse phase and does not mute the first step again");
            std::vector<std::uint8_t> bytes;
            Core::StateArchive writer(bytes);
            writer(apu);
            APU restored;
            restored.setSampleRate(APU::ClockRate);
            Core::StateArchive reader(static_cast<const std::vector<std::uint8_t> &>(bytes));
            reader(restored);
            reader.finish();
            for (unsigned i = 0; i < 64; ++i)
            {
                apu.tick();
                restored.tick();
                check((apu.takeSamples().back() < 0) == (restored.takeSamples().back() < 0),
                      "save state restores pulse phase and its running clock");
            }
        }

        for (unsigned channel : {0u, 1u, 3u})
            for (unsigned phase : {6u, 7u})
            {
                APU apu;
                for (unsigned i = 0; i < phase; ++i)
                    apu.clockFrameSequencer();
                const std::uint16_t envelope = channel == 0 ? 0xFF12 : channel == 1 ? 0xFF17
                                                                                    : 0xFF21;
                const std::uint16_t trigger = channel == 0 ? 0xFF14 : channel == 1 ? 0xFF19
                                                                                   : 0xFF23;
                apu.write(envelope, 0xF1);
                apu.write(trigger, 0x80);
                for (unsigned i = phase; i < 8; ++i)
                    apu.clockFrameSequencer();
                check(apu.channelVolume(channel) == (phase == 7 ? 15 : 14), "envelope trigger delay depends on next sequencer step");
                for (unsigned i = 0; i < 8; ++i)
                    apu.clockFrameSequencer();
                check(apu.channelVolume(channel) == (phase == 7 ? 14 : 13), "envelope resumes at its normal pace");
            }

        // O período zero no passo 7 recarrega 9, um estado válido para snapshots.
        APU apu;
        for (unsigned i = 0; i < 7; ++i)
            apu.clockFrameSequencer();
        apu.write(0xFF12, 0xF0);
        apu.write(0xFF14, 0x80);
        std::vector<std::uint8_t> bytes;
        Core::StateArchive writer(bytes);
        writer(apu);
        APU restored;
        Core::StateArchive reader(static_cast<const std::vector<std::uint8_t> &>(bytes));
        reader(restored);
        reader.finish();
        check(restored.channelVolume(0) == 15, "save state accepts the extended envelope timer");
    }

    void disconnectedDacs()
    {
        Core::APU::APU apu;
        apu.setSampleRate(Core::APU::APU::ClockRate);
        apu.write(0xFF25, 0x11);
        apu.write(0xFF12, 0x08); // DAC ligado, gerador inativo: carrega o capacitor com DC.
        for (unsigned i = 0; i < 100000; ++i)
            apu.tick();
        apu.takeSamples();
        apu.write(0xFF12, 0);
        for (unsigned i = 0; i < 100000; ++i)
            apu.tick();
        const auto silence = apu.takeSamples();
        check(std::all_of(silence.begin(), silence.end(), [](float s)
                          { return s == 0; }),
              "all DACs off disconnects both outputs immediately");
        apu.write(0xFF12, 0x08);
        apu.tick();
        const auto reconnected = apu.takeSamples();
        check(reconnected[0] > 0 && reconnected[0] < 0.01f, "disconnected capacitor retains its charge");
        apu.write(0xFF26, 0);
        apu.tick();
        check(apu.takeSamples()[0] == 0, "power off also disconnects the charged capacitor");
    }

    void poweredOffLengths()
    {
        // A ROM é DMG em ambos: a escolha depende do console físico.
        for (auto model : {Core::Model::DMG, Core::Model::CGB})
            for (bool writeWhileOff : {false, true})
            {
                Core::Board board(model);
                idle(board);
                for (auto address : {0xFF11, 0xFF16, 0xFF20})
                    board.write(address, 0x3F); // length 1
                board.write(0xFF1B, 0xFF);
                board.write(0xFF26, 0);
                if (writeWhileOff)
                {
                    for (auto address : {0xFF11, 0xFF16, 0xFF20})
                        board.write(address, 0xFE); // length 2; duty não deve mudar
                    board.write(0xFF1B, 0xFE);
                }
                const auto state = board.saveState();
                board.reset();
                board.loadState(state);
                board.write(0xFF04, 0);
                run(board, 4); // observa DIV zerado ainda com APU desligada
                board.write(0xFF26, 0x80);
                check(board.read(0xFF11) == 0x3F && board.read(0xFF16) == 0x3F, "off writes never restore duty bits");
                for (auto address : {0xFF12, 0xFF17, 0xFF21})
                    board.write(address, 0xF0);
                board.write(0xFF1A, 0x80);
                for (auto address : {0xFF14, 0xFF19, 0xFF1E, 0xFF23})
                    board.write(address, 0xC0);
                run(board, stepCycles);
                check((board.read(0xFF26) & 15) == (model == Core::Model::DMG && !writeWhileOff ? 0 : 15),
                      "DMG retains length while CGB clears it across power off and save state");
                run(board, 2 * stepCycles);
                check((board.read(0xFF26) & 15) == (model == Core::Model::DMG ? 0 : 15),
                      "only DMG accepts length writes while powered off");
            }
    }

    void outputChannelPreferences()
    {
        for (unsigned channel = 0; channel < 4; ++channel)
        {
            Core::Board board(Core::Model::CGB);
            idle(board);
            board.write(0xFF11, 0x80);
            board.write(0xFF16, 0x80);
            for (unsigned i = 0; i < 16; ++i)
                board.write(0xFF30 + i, 0xF0);
            if (channel < 2)
            {
                const auto offset = channel * 5;
                board.write(0xFF12 + offset, 0xF0);
                board.write(0xFF14 + offset, 0x87);
            }
            else if (channel == 2)
            {
                board.write(0xFF1A, 0x80);
                board.write(0xFF1C, 0x20);
                board.write(0xFF1E, 0x87);
            }
            else
            {
                board.write(0xFF21, 0xF0);
                board.write(0xFF23, 0x80);
            }
            const auto audible = capture(board, 50000);
            check(audible.hi - audible.lo > 0.1f, "each enabled channel reaches the output");
            const auto hardware = board.saveState();
            const auto mask = static_cast<std::uint8_t>(15 & ~(1 << channel));
            board.setAudioOutputChannels(mask);
            check(board.saveState() == hardware, "muting changes no emulated hardware or snapshot data");
            const auto muted = capture(board, 50000);
            check(muted.lo == 0 && muted.hi == 0 && muted.loR == 0 && muted.hiR == 0,
                  "each muted channel is removed from both outputs");
            check(board.apu().channelActive(channel), "muted channels keep running");
            board.loadState(hardware);
            check(board.apu().outputChannels() == mask, "load state preserves channel preferences");
            board.write(0xFF26, 0);
            check(board.apu().outputChannels() == mask, "APU power off preserves channel preferences");
            board.reset();
            check(board.apu().outputChannels() == mask, "reset preserves channel preferences");
            board.unloadCartridge();
            idle(board);
            check(board.apu().outputChannels() == mask, "ROM changes preserve channel preferences");
            board.loadState(hardware);
            board.setAudioOutputChannels(15);
            const auto resumed = capture(board, 50000);
            check(resumed.hi - resumed.lo > 0.1f, "unmuting restores channel output");
        }
    }

    void powerAndState()
    {
        Core::Board board(Core::Model::CGB);
        idle(board);
        board.write(0xFF11, 0x80);
        board.write(0xFF12, 0xF3);
        board.write(0xFF13, 0x40);
        board.write(0xFF14, 0x83);
        board.write(0xFF1A, 0x80);
        board.write(0xFF1E, 0x83);
        board.write(0xFF21, 0xF1);
        board.write(0xFF23, 0x80);
        board.write(0xFF30, 0x5A);
        run(board, 100000);
        const auto state = board.saveState();
        const auto nr52 = board.read(0xFF26);
        run(board, 300000);
        const auto expectedVolume = board.apu().channelVolume(0);
        const auto expectedFrequency = board.apu().channelFrequency(0);
        const auto expectedNr52 = board.read(0xFF26);
        board.loadState(state);
        check(board.read(0xFF26) == nr52, "APU state restores channels");
        run(board, 300000);
        check(board.apu().channelVolume(0) == expectedVolume && board.apu().channelFrequency(0) == expectedFrequency &&
                  board.read(0xFF26) == expectedNr52,
              "restored APU continues deterministically");

        // Desligar a APU zera todos os registradores e canais, e bloqueia escritas.
        board.write(0xFF26, 0x00);
        for (std::uint16_t a = 0xFF10; a <= 0xFF25; ++a)
            check(board.read(a) == (a == 0xFF10 ? 0x80 : (a == 0xFF11 || a == 0xFF16)                                                                          ? 0x3F
                                                     : (a == 0xFF1A)                                                                                           ? 0x7F
                                                     : (a == 0xFF1C)                                                                                           ? 0x9F
                                                     : (a == 0xFF14 || a == 0xFF19 || a == 0xFF1E || a == 0xFF23)                                              ? 0xBF
                                                     : (a == 0xFF13 || a == 0xFF18 || a == 0xFF1D || a == 0xFF1B || a == 0xFF20 || a == 0xFF15 || a == 0xFF1F) ? 0xFF
                                                                                                                                                               : 0x00),
                  "power off clears registers");
        check(board.read(0xFF26) == 0x70, "power off clears channel status");
        board.write(0xFF12, 0xF0);
        board.write(0xFF14, 0x80);
        check(board.read(0xFF12) == 0 && !(board.read(0xFF26) & 1), "writes ignored while APU is off");
        run(board, 1000000);
        const auto silent = capture(board, 50000);
        check(silent.hi - silent.lo < 0.01f, "no output while APU is off");
        // Um estado salvo inválido é rejeitado sem alterar a APU.
        auto bad = state;
        bad[bad.size() / 2] ^= 0xFF;
        bool rejected = false;
        try
        {
            board.loadState(bad);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check(rejected, "corrupted save state is rejected");
    }
}

int main()
{
    try
    {
        controls(Core::Model::DMG);
        controls(Core::Model::CGB);
        registerMasks();
        lengthCounter();
        dacAndEnvelope();
        sweep();
        waveRam();
        synthesis();
        triggerEdges();
        disconnectedDacs();
        poweredOffLengths();
        outputChannelPreferences();
        powerAndState();
        std::cout << "APU control tests passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}