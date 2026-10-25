#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace R2BOY::System
{
    // Histórico circular em memória; usa o mesmo formato dos save states da Board.
    // O tempo é emulado, portanto fast-forward não aumenta a janela de dez segundos.
    class RewindBuffer
    {
    public:
        static constexpr std::uint64_t FrameCycles = 70224;
        static constexpr std::uint64_t MaxCycles = 4194304ull * 10;
        struct Snapshot
        {
            std::uint64_t cycles = 0;
            std::vector<std::uint8_t> state;
        };

        explicit RewindBuffer(unsigned interval = 5) { configure(interval); }

        void configure(unsigned interval)
        {
            if (interval != 1 && interval != 5 && interval != 10)
                throw std::invalid_argument("Invalid rewind interval.");
            interval_ = interval;
            slots_ = std::vector<Snapshot>(MaxCycles / (FrameCycles * interval_) + 1);
            clear();
        }

        void clear()
        {
            for (auto &slot : slots_)
                slot = {};
            head_ = count_ = 0;
            nextCaptureFrame_ = 0;
        }

        bool captureDue(std::uint64_t cycles) const
        {
            return cycles / FrameCycles >= nextCaptureFrame_;
        }

        void push(std::uint64_t cycles, std::vector<std::uint8_t> state)
        {
            expire(cycles);
            if (count_ == slots_.size())
                removeOldest();
            slots_[(head_ + count_) % slots_.size()] = {cycles, std::move(state)};
            ++count_;
            nextCaptureFrame_ = cycles / FrameCycles + interval_;
        }

        // Consome o futuro: após soltar CAPSLOCK, a gravação segue a nova linha do tempo.
        std::optional<Snapshot> popBefore(std::uint64_t cycles)
        {
            expire(cycles);
            while (count_)
            {
                auto &slot = slots_[(head_ + count_ - 1) % slots_.size()];
                auto snapshot = std::move(slot);
                slot = {};
                --count_;
                if (snapshot.cycles < cycles)
                {
                    nextCaptureFrame_ = snapshot.cycles / FrameCycles + interval_;
                    return snapshot;
                }
            }
            return std::nullopt;
        }

        std::size_t size() const { return count_; }
        unsigned interval() const { return interval_; }

    private:
        void removeOldest()
        {
            slots_[head_] = {};
            head_ = (head_ + 1) % slots_.size();
            --count_;
        }

        void expire(std::uint64_t cycles)
        {
            while (count_ && cycles > slots_[head_].cycles &&
                   cycles - slots_[head_].cycles > MaxCycles)
                removeOldest();
        }

        std::vector<Snapshot> slots_;
        std::size_t head_ = 0, count_ = 0;
        unsigned interval_ = 5;
        std::uint64_t nextCaptureFrame_ = 0;
    };
}