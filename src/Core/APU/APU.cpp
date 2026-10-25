#include "APU.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace R2BOY::Core::APU
{
    namespace
    {
        // Padrões de duty de 8 passos (bit 0 = primeiro passo): 12,5%, 25%, 50% e 75%.
        constexpr std::uint8_t dutyPatterns[4] = {0x01, 0x81, 0x87, 0x7E};
        constexpr std::uint8_t noiseDivisors[8] = {8, 16, 32, 48, 64, 80, 96, 112};
        // Limita a fila de saída (~1 s estéreo) caso ninguém consuma as amostras.
        constexpr std::size_t maxQueuedSamples = 2 * 48000;
        // Fator do capacitor (corte de dezenas de Hz), elevado a ClockRate/taxa por amostra.
        constexpr double capacitorCharge = 0.999958;
    }

    void APU::reset(bool cgb)
    {
        const auto rate = sampleRate_;
        const auto outputChannels = outputChannels_;
        const auto outputVolumes = outputVolumes_;
        *this = APU(nullptr);
        sampleRate_ = rate;
        outputChannels_ = outputChannels;
        outputVolumes_ = outputVolumes;
        cgb_ = cgb;
    }

    std::uint8_t APU::waveIndexForAccess() const { return wavePosition_ >> 1; }

    void APU::powerOff()
    {
        // O DMG preserva length counters; ambos os modelos preservam Wave RAM.
        const auto rate = sampleRate_;
        const auto cgb = cgb_;
        const auto outputChannels = outputChannels_;
        const auto outputVolumes = outputVolumes_;
        std::array<std::uint16_t, 4> lengths{};
        for (unsigned i = 0; i < 4; ++i)
            lengths[i] = channels_[i].length;
        auto wave = waveRam_;
        auto samples = std::move(samples_);
        auto left = capacitorLeft_, right = capacitorRight_;
        *this = APU(nullptr);
        sampleRate_ = rate;
        cgb_ = cgb;
        outputChannels_ = outputChannels;
        outputVolumes_ = outputVolumes;
        if (!cgb_)
            for (unsigned i = 0; i < 4; ++i)
                channels_[i].length = lengths[i];
        waveRam_ = wave;
        samples_ = std::move(samples);
        capacitorLeft_ = left;
        capacitorRight_ = right;
        powered_ = false;
        volume_ = panning_ = 0;
    }

    std::uint8_t APU::read(std::uint16_t address) const
    {
        if (address >= 0xFF30 && address <= 0xFF3F)
        {
            // Com o canal de onda ativo, o acesso vai ao byte em leitura (comportamento CGB).
            return waveRam_[channels_[2].enabled ? waveIndexForAccess() : address - 0xFF30];
        }
        const auto &c = channels_;
        switch (address)
        {
        case 0xFF10:
            return 0x80 | sweepRegister_;
        case 0xFF11:
            return 0x3F | (duty_[0] << 6);
        case 0xFF12:
            return c[0].nr2;
        case 0xFF14:
            return 0xBF | (c[0].lengthEnabled ? 0x40 : 0);
        case 0xFF16:
            return 0x3F | (duty_[1] << 6);
        case 0xFF17:
            return c[1].nr2;
        case 0xFF19:
            return 0xBF | (c[1].lengthEnabled ? 0x40 : 0);
        case 0xFF1A:
            return 0x7F | (c[2].dac ? 0x80 : 0);
        case 0xFF1C:
            return 0x9F | (waveLevel_ << 5);
        case 0xFF1E:
            return 0xBF | (c[2].lengthEnabled ? 0x40 : 0);
        case 0xFF21:
            return c[3].nr2;
        case 0xFF22:
            return noiseRegister_;
        case 0xFF23:
            return 0xBF | (c[3].lengthEnabled ? 0x40 : 0);
        case 0xFF24:
            return volume_;
        case 0xFF25:
            return panning_;
        case 0xFF26:
            return 0x70 | (powered_ ? 0x80 : 0) | (c[0].enabled ? 1 : 0) | (c[1].enabled ? 2 : 0) |
                   (c[2].enabled ? 4 : 0) | (c[3].enabled ? 8 : 0);
        default:
            return 0xFF;
        }
    }

    void APU::write(std::uint16_t address, std::uint8_t value)
    {
        if (address >= 0xFF30 && address <= 0xFF3F)
        {
            waveRam_[channels_[2].enabled ? waveIndexForAccess() : address - 0xFF30] = value;
            return;
        }
        if (address == 0xFF26)
        {
            const bool on = (value & 0x80) != 0;
            if (on && !powered_)
            {
                powered_ = true;
                frameStep_ = 0;
                dutyPosition_ = {0, 0};
                wavePosition_ = 0;
                waveSample_ = 0;
            }
            else if (!on && powered_)
                powerOff();
            return;
        }
        if (!powered_)
        {
            // No DMG apenas os bits de length de NRx1 aceitam escritas desligado.
            if (!cgb_)
            {
                switch (address)
                {
                case 0xFF11:
                    channels_[0].length = 64 - (value & 0x3F);
                    break;
                case 0xFF16:
                    channels_[1].length = 64 - (value & 0x3F);
                    break;
                case 0xFF1B:
                    channels_[2].length = 256 - value;
                    break;
                case 0xFF20:
                    channels_[3].length = 64 - (value & 0x3F);
                    break;
                default:
                    break;
                }
            }
            return;
        }
        if (address < 0xFF10 || address > 0xFF25)
            return;

        auto &c = channels_;
        const auto dac = [&](unsigned i)
        {
            c[i].dac = (value & 0xF8) != 0;
            if (!c[i].dac)
                c[i].enabled = false;
        };
        const auto envelope = [&](unsigned i)
        {
            c[i].nr2 = value;
            dac(i);
        };
        const auto highBits = [&](unsigned i)
        {
            c[i].frequency = static_cast<std::uint16_t>((c[i].frequency & 0xFF) | ((value & 7) << 8));
            enableLength(i, (value & 0x40) != 0);
            if (value & 0x80)
                trigger(i);
        };

        switch (address)
        {
        case 0xFF10:
            // Passar de subtração para adição após um cálculo em modo negativo desliga o canal.
            if (sweepNegated_ && (sweepRegister_ & 8) && !(value & 8))
                c[0].enabled = false;
            sweepRegister_ = value & 0x7F;
            break;
        case 0xFF11:
            duty_[0] = value >> 6;
            c[0].length = 64 - (value & 0x3F);
            break;
        case 0xFF12:
            envelope(0);
            break;
        case 0xFF13:
            c[0].frequency = static_cast<std::uint16_t>((c[0].frequency & 0x700) | value);
            break;
        case 0xFF14:
            highBits(0);
            break;
        case 0xFF16:
            duty_[1] = value >> 6;
            c[1].length = 64 - (value & 0x3F);
            break;
        case 0xFF17:
            envelope(1);
            break;
        case 0xFF18:
            c[1].frequency = static_cast<std::uint16_t>((c[1].frequency & 0x700) | value);
            break;
        case 0xFF19:
            highBits(1);
            break;
        case 0xFF1A:
            c[2].dac = (value & 0x80) != 0;
            if (!c[2].dac)
                c[2].enabled = false;
            break;
        case 0xFF1B:
            c[2].length = static_cast<std::uint16_t>(256 - value);
            break;
        case 0xFF1C:
            waveLevel_ = (value >> 5) & 3;
            break;
        case 0xFF1D:
            c[2].frequency = static_cast<std::uint16_t>((c[2].frequency & 0x700) | value);
            break;
        case 0xFF1E:
            highBits(2);
            break;
        case 0xFF20:
            c[3].length = 64 - (value & 0x3F);
            break;
        case 0xFF21:
            envelope(3);
            break;
        case 0xFF22:
            noiseRegister_ = value;
            break;
        case 0xFF23:
            enableLength(3, (value & 0x40) != 0);
            if (value & 0x80)
                trigger(3);
            break;
        case 0xFF24:
            volume_ = value;
            break;
        case 0xFF25:
            panning_ = value;
            break;
        default:
            break;
        }
    }

    // Ao habilitar o length com o próximo passo do sequenciador sem clock de length,
    // o contador recebe um clock extra (quirk documentado na Pan Docs).
    void APU::enableLength(unsigned index, bool enable)
    {
        auto &c = channels_[index];
        const bool rising = enable && !c.lengthEnabled;
        c.lengthEnabled = enable;
        if (rising && (frameStep_ & 1) && c.length != 0 && --c.length == 0)
            c.enabled = false;
    }

    std::uint32_t APU::period(unsigned index) const
    {
        const auto &c = channels_[index];
        if (index == 2)
            return (2048u - c.frequency) * 2;
        if (index == 3)
        {
            const unsigned shift = noiseRegister_ >> 4;
            return static_cast<std::uint32_t>(noiseDivisors[noiseRegister_ & 7]) << std::min(shift, 15u);
        }
        return (2048u - c.frequency) * 4;
    }

    std::uint16_t APU::sweepTarget()
    {
        const auto delta = static_cast<std::uint16_t>(sweepShadow_ >> (sweepRegister_ & 7));
        std::uint16_t target;
        if (sweepRegister_ & 8)
        {
            target = static_cast<std::uint16_t>(sweepShadow_ - delta);
            sweepNegated_ = true;
        }
        else
            target = static_cast<std::uint16_t>(sweepShadow_ + delta);
        if (target > 2047)
            channels_[0].enabled = false;
        return target;
    }

    void APU::trigger(unsigned index)
    {
        auto &c = channels_[index];
        c.enabled = c.dac;
        if (c.length == 0)
        {
            c.length = maxLength(index);
            // Trigger com length habilitado e passo sem clock de length: já entra decrementado.
            if (c.lengthEnabled && (frameStep_ & 1))
                --c.length;
        }
        c.timer = period(index);
        if (index < 2)
            dutyStarted_[index] = true;
        if (index != 2)
        {
            c.volume = c.nr2 >> 4;
            c.envelopeTimer = (c.nr2 & 7) ? (c.nr2 & 7) : 8;
            // O próximo clock de envelope não conta como um período completo.
            if (frameStep_ == 7)
                ++c.envelopeTimer;
        }
        if (index == 3)
            lfsr_ = 0x7FFF;
        if (index == 2)
        {
            wavePosition_ = 0;
        }
        if (index == 0)
        {
            sweepShadow_ = c.frequency;
            sweepNegated_ = false;
            const auto pace = (sweepRegister_ >> 4) & 7;
            sweepTimer_ = pace ? pace : 8;
            sweepEnabled_ = pace != 0 || (sweepRegister_ & 7) != 0;
            if (sweepRegister_ & 7)
                sweepTarget();
        }
    }

    void APU::clockLength(unsigned index)
    {
        auto &c = channels_[index];
        if (c.lengthEnabled && c.length != 0 && --c.length == 0)
            c.enabled = false;
    }

    void APU::clockEnvelope(unsigned index)
    {
        auto &c = channels_[index];
        if (!c.enabled || (c.nr2 & 7) == 0)
            return;
        if (c.envelopeTimer && --c.envelopeTimer == 0)
        {
            c.envelopeTimer = c.nr2 & 7;
            if ((c.nr2 & 8) && c.volume < 15)
                ++c.volume;
            else if (!(c.nr2 & 8) && c.volume > 0)
                --c.volume;
        }
    }

    void APU::clockSweep()
    {
        if (sweepTimer_ && --sweepTimer_ == 0)
        {
            const auto pace = (sweepRegister_ >> 4) & 7;
            sweepTimer_ = pace ? pace : 8;
            if (sweepEnabled_ && pace)
            {
                const auto target = sweepTarget();
                if (target <= 2047 && (sweepRegister_ & 7))
                {
                    sweepShadow_ = channels_[0].frequency = target;
                    sweepTarget();
                }
            }
        }
    }

    void APU::clockFrameSequencer()
    {
        if (!powered_)
            return;
        // Passos: 0,2,4,6 length; 2,6 sweep; 7 envelope.
        if (!(frameStep_ & 1))
            for (unsigned i = 0; i < 4; ++i)
                clockLength(i);
        if (frameStep_ == 2 || frameStep_ == 6)
            clockSweep();
        if (frameStep_ == 7)
        {
            clockEnvelope(0);
            clockEnvelope(1);
            clockEnvelope(3);
        }
        frameStep_ = (frameStep_ + 1) & 7;
    }

    void APU::tick()
    {
        if (powered_)
        {
            for (unsigned i = 0; i < 4; ++i)
            {
                auto &c = channels_[i];
                if (i < 2 && !dutyStarted_[i])
                    continue;
                if (c.timer > 1)
                {
                    --c.timer;
                    continue;
                }
                c.timer = period(i);
                if (i == 3 && (noiseRegister_ >> 4) >= 14)
                    continue; // Com shift 14/15 o LFSR não recebe clock.
                if (i < 2)
                {
                    dutyPosition_[i] = (dutyPosition_[i] + 1) & 7;
                    dutyFirstSample_[i] = false;
                }
                else if (i == 2)
                {
                    wavePosition_ = (wavePosition_ + 1) & 31;
                    const auto byte = waveRam_[wavePosition_ >> 1];
                    waveSample_ = (wavePosition_ & 1) ? (byte & 0x0F) : (byte >> 4);
                }
                else
                {
                    const auto bit = static_cast<std::uint16_t>((lfsr_ ^ (lfsr_ >> 1)) & 1);
                    lfsr_ = static_cast<std::uint16_t>((lfsr_ >> 1) | (bit << 14));
                    if (noiseRegister_ & 8)
                        lfsr_ = static_cast<std::uint16_t>((lfsr_ & ~0x40) | (bit << 6));
                }
            }
            for (unsigned i = 0; i < 4; ++i)
            {
                auto &c = channels_[i];
                std::uint8_t out = 0;
                if (c.enabled)
                {
                    if (i < 2)
                        out = !dutyFirstSample_[i] && ((dutyPatterns[duty_[i]] >> dutyPosition_[i]) & 1) ? c.volume : 0;
                    else if (i == 2)
                        out = waveLevel_ ? waveSample_ >> (waveLevel_ - 1) : 0;
                    else
                        out = (lfsr_ & 1) ? 0 : c.volume;
                }
                c.output = out;
                // DAC: nível digital 0..15 mapeado para +1..-1; DAC desligado emite 0.
                channelSum_[i] += c.dac ? 1.0f - 2.0f * out / 15.0f : 0.0f;
            }
        }
        ++sampleTicks_;
        sampleAccumulator_ += sampleRate_;
        if (sampleAccumulator_ >= ClockRate)
        {
            sampleAccumulator_ -= ClockRate;
            emit();
        }
    }

    void APU::emit()
    {
        float left = 0, right = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            const float level = channelSum_[i] / static_cast<float>(sampleTicks_) *
                                static_cast<float>(outputVolumes_[i]) / 100.0f;
            if ((outputChannels_ & (1 << i)) && (panning_ & (0x10 << i)))
                left += level;
            if ((outputChannels_ & (1 << i)) && (panning_ & (1 << i)))
                right += level;
            channelSum_[i] = 0;
        }
        sampleTicks_ = 0;
        left = left / 4.0f * static_cast<float>(((volume_ >> 4) & 7) + 1) / 8.0f;
        right = right / 4.0f * static_cast<float>((volume_ & 7) + 1) / 8.0f;

        // Filtro passa-altas do capacitor de acoplamento, aplicado à taxa de saída.
        const float charge = static_cast<float>(std::pow(capacitorCharge, static_cast<double>(ClockRate) / sampleRate_));
        float outLeft = 0, outRight = 0;
        bool dacsEnabled = false;
        for (unsigned i = 0; i < 4; ++i)
            dacsEnabled |= (outputChannels_ & (1 << i)) && channels_[i].dac;
        if (dacsEnabled)
        {
            outLeft = left - capacitorLeft_;
            outRight = right - capacitorRight_;
            capacitorLeft_ = left - outLeft * charge;
            capacitorRight_ = right - outRight * charge;
        }

        if (samples_.size() + 2 > maxQueuedSamples)
            return;
        samples_.push_back(outLeft);
        samples_.push_back(outRight);
    }

    void APU::setSampleRate(std::uint32_t rate)
    {
        sampleRate_ = std::clamp<std::uint32_t>(rate, 8000, ClockRate);
        sampleAccumulator_ = 0;
        sampleTicks_ = 0;
        channelSum_.fill(0);
        samples_.clear();
    }

    std::vector<float> APU::takeSamples() { return std::exchange(samples_, {}); }
    void APU::clearSamples() { samples_.clear(); }

    void APU::setOutputVolume(unsigned channel, int percent)
    {
        if (channel >= outputVolumes_.size())
            return;
        percent = std::clamp(percent, 0, 100);
        if (outputVolumes_[channel] == percent)
            return;
        outputVolumes_[channel] = percent;
        setSampleRate(sampleRate_);
        capacitorLeft_ = capacitorRight_ = 0;
    }

    void APU::setOutputChannels(std::uint8_t mask)
    {
        mask &= 0x0F;
        if (outputChannels_ == mask)
            return;
        outputChannels_ = mask;
        setSampleRate(sampleRate_);
        capacitorLeft_ = capacitorRight_ = 0;
    }

    void APU::state(StateArchive &archive)
    {
        archive(powered_, volume_, panning_, channels_, frameStep_, duty_, dutyPosition_, sweepRegister_,
                sweepShadow_, sweepTimer_, sweepEnabled_, sweepNegated_, waveLevel_, wavePosition_, waveSample_,
                waveRam_, noiseRegister_, lfsr_, dutyStarted_, dutyFirstSample_);
        if (archive.reading())
        {
            // Valida campos que indexam tabelas ou deslocamentos.
            if (frameStep_ > 7 || duty_[0] > 3 || duty_[1] > 3 || waveLevel_ > 3 || wavePosition_ > 31 ||
                waveSample_ > 15 || dutyPosition_[0] > 7 || dutyPosition_[1] > 7)
                throw std::runtime_error("Invalid APU save state.");
            for (const auto &c : channels_)
                if (c.volume > 15 || c.envelopeTimer > 9 || c.length > 256 || c.frequency > 2047)
                    throw std::runtime_error("Invalid APU save state.");
            if (lfsr_ > 0x7FFF)
                throw std::runtime_error("Invalid APU save state.");
        }
    }
}