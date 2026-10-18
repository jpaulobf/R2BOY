#include "Board.h"

#include <stdexcept>
#include <utility>

namespace R2BOY::Core
{
    Board::Board(Model model) : model_(model), bus_(cartridge_, memory_, interrupts_, serial_, timer_),
                                cpu_(bus_, [this]()
                                     { tickMachineCycle(); })
    {
    }

    void Board::loadCartridge(std::vector<std::uint8_t> rom, std::string source)
    {
        Cartridge::Cartridge next;
        next.load(std::move(rom), std::move(source));
        loadCartridge(std::move(next));
    }

    void Board::loadCartridge(Cartridge::Cartridge next)
    {
        if (!next.loaded())
            throw std::runtime_error("Cannot insert an empty cartridge.");

        if (model_ == Model::DMG && next.header().cgbFlag == 0xC0)
            throw std::runtime_error("This cartridge requires Game Boy Color hardware.");

        const auto mode = model_ == Model::CGB && (next.header().cgbFlag & 0x80) != 0
                              ? ExecutionMode::CGB
                              : ExecutionMode::DMG;
        cartridge_ = std::move(next);
        executionMode_ = mode;
        reset();
    }

    void Board::resetMemory()
    {
        memory_.reset(executionMode_ == ExecutionMode::CGB);
        cartridge_.resetBanks();
    }

    void Board::reset()
    {
        clockCycles_ = 0;
        resetMemory();
        interrupts_.reset(true);
        serial_.reset(executionMode_ == ExecutionMode::CGB);

        // Perfil determinístico sem boot ROM: fases conhecidas DMG/CGB, sem
        // reproduzir o tempo variável da animação/interação da boot ROM original.
        timer_.reset(model_ == Model::CGB, model_ == Model::DMG ? 0xABCC : (executionMode_ == ExecutionMode::CGB ? 0x1EA0 : 0x267C));
        CPU::CPU::Registers registers;
        registers.pc = 0x0100;
        registers.sp = 0xFFFE;
        if (model_ == Model::DMG)
        {
            registers.setAF(cartridge_.loaded() && cartridge_.header().headerChecksum == 0 ? 0x0180 : 0x01B0);
            registers.setBC(0x0013);
            registers.setDE(0x00D8);
            registers.setHL(0x014D);
        }
        else
        {
            registers.setAF(0x1180);
            if (executionMode_ == ExecutionMode::CGB)
            {
                registers.setDE(0xFF56);
                registers.setHL(0x000D);
            }
            else
            {
                // Em compatibilidade DMG, o CGB deixa em B a soma do título para
                // jogos Nintendo; HL também depende desse identificador de paleta.
                if (cartridge_.loaded())
                {
                    const auto &header = cartridge_.header();
                    if (header.oldLicensee == 1 || (header.oldLicensee == 0x33 && header.newLicensee == "01"))
                        for (std::size_t i = 0x134; i <= 0x143; ++i)
                            registers.b = static_cast<std::uint8_t>(registers.b + cartridge_.rom()[i]);
                }
                registers.setDE(0x0008);
                registers.setHL(registers.b == 0x43 || registers.b == 0x58 ? 0x991A : 0x007C);
            }
        }
        cpu_.reset(registers);
    }

    unsigned Board::step()
    {
        if (!cartridge_.loaded())
            return 0;
        return cpu_.step();
    }

    void Board::tickMachineCycle()
    {
        for (unsigned t = 0; t < 4; ++t)
        {
            ++clockCycles_;
            timer_.tick(interrupts_);
            serial_.tick(1, interrupts_);
            // PPU e DMA serão conectados aqui, sem alterar o decoder da CPU.
        }
    }

    Model Board::model() const
    {
        return model_;
    }

    ExecutionMode Board::executionMode() const
    {
        return executionMode_;
    }
}