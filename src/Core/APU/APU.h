#pragma once

#include "Core/StateArchive.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace R2BOY::Core::APU
{
    /**
     * APU completa do Game Boy (registradores FF10–FF26 e Wave RAM FF30–FF3F),
     * conforme a Pan Docs (gbdev.io): dois canais de pulso (o primeiro com sweep),
     * canal de onda e canal de ruído, com length counter, envelope, frame sequencer,
     * mixer NR50/NR51 e filtro passa-altas do capacitor de saída.
     *
     * O Core não depende de plataforma: tick() avança um T-cycle do clock base
     * (4.194.304 Hz) e as amostras estéreo intercaladas (L, R em [-1, 1]) ficam
     * numa fila lida por takeSamples(). A saída física cabe a System/Audio.
     * Modelo de referência: comportamento CGB (escritas em NRx1 com a APU desligada
     * são ignoradas e o desligamento zera também os length counters).
     */
    class APU
    {
    public:
        static constexpr std::uint32_t ClockRate = 4194304;

        APU() = default;

        // Estado pós-boot (sem boot ROM): APU ligada, NR50=77, NR51=F3, canal 1 sem DAC ativo.
        void reset();
        std::uint8_t read(std::uint16_t address) const;
        void write(std::uint16_t address, std::uint8_t value);

        // Avança um T-cycle do clock base: timers dos canais e geração de amostras.
        void tick();

        // O frame sequencer avança na borda de descida do bit 4 de DIV (bit 5 em double
        // speed). A Board informa essa borda, o que inclui reinícios de DIV por escrita.
        void clockFrameSequencer();

        // Bit do contador do sistema que aciona o frame sequencer (12, ou 13 em double speed).
        static constexpr unsigned frameSequencerBit(bool doubleSpeed) { return doubleSpeed ? 13 : 12; }

        // Frequência de amostragem de saída; descarta amostras pendentes.
        void setSampleRate(std::uint32_t rate);
        std::uint32_t sampleRate() const { return sampleRate_; }

        // Retorna e remove as amostras estéreo intercaladas geradas desde a última chamada.
        std::vector<float> takeSamples();
        void clearSamples();

        // Estado dos canais (bits de NR52), para diagnóstico e testes.
        bool channelActive(unsigned channel) const { return channel < 4 && channels_[channel].enabled; }
        std::uint8_t channelVolume(unsigned channel) const { return channel < 4 ? channels_[channel].volume : 0; }
        std::uint16_t channelFrequency(unsigned channel) const { return channel < 4 ? channels_[channel].frequency : 0; }

    private:
        explicit APU(std::nullptr_t) {}
        friend class R2BOY::Core::StateArchive;

        struct Channel
        {
            bool enabled = false;
            bool dac = false;
            bool lengthEnabled = false;
            std::uint16_t length = 0;
            std::uint32_t timer = 0;
            std::uint16_t frequency = 0;
            std::uint8_t nr2 = 0; // NRx2 como escrito; o volume corrente fica em volume.
            std::uint8_t volume = 0;
            std::uint8_t envelopeTimer = 0;
            std::uint8_t output = 0;
            void state(StateArchive &a)
            {
                a(enabled, dac, lengthEnabled, length, timer, frequency, nr2, volume,
                  envelopeTimer, output);
            }
        };

        void state(StateArchive &archive);
        void powerOff();
        void trigger(unsigned index);
        void clockLength(unsigned index);
        void clockEnvelope(unsigned index);
        void clockSweep();
        std::uint16_t sweepTarget();
        void enableLength(unsigned index, bool enable);
        std::uint32_t period(unsigned index) const;
        void emit();
        std::uint8_t waveIndexForAccess() const;

        static constexpr std::uint16_t maxLength(unsigned index) { return index == 2 ? 256 : 64; }

        bool powered_ = true;
        std::uint8_t volume_ = 0x77;
        std::uint8_t panning_ = 0xF3;
        std::array<Channel, 4> channels_{};
        std::uint8_t frameStep_ = 0;
        std::array<std::uint8_t, 2> duty_{};
        std::array<std::uint8_t, 2> dutyPosition_{};
        std::uint8_t sweepRegister_ = 0;
        std::uint16_t sweepShadow_ = 0;
        std::uint8_t sweepTimer_ = 0;
        bool sweepEnabled_ = false;
        bool sweepNegated_ = false;
        std::uint8_t waveLevel_ = 0;
        std::uint8_t wavePosition_ = 0;
        std::uint8_t waveSample_ = 0;
        std::array<std::uint8_t, 16> waveRam_{};
        std::uint8_t noiseRegister_ = 0;
        std::uint16_t lfsr_ = 0x7FFF;

        // Saída: não serializada, pois pertence à sessão de áudio e não ao hardware.
        std::uint32_t sampleRate_ = 48000;
        std::uint32_t sampleAccumulator_ = 0;
        std::uint32_t sampleTicks_ = 0;
        std::array<float, 4> channelSum_{};
        float capacitorLeft_ = 0, capacitorRight_ = 0;
        std::vector<float> samples_;
    };
}
