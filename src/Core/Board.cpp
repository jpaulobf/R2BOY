#include "Board.h"
#include "Common/RomFile.h"
#include <stdexcept>
#include <utility>

namespace R2BOY::Core
{
    Board::Board(Model model) : model_(model), bus_(cartridge_, memory_, interrupts_, serial_, timer_, dma_, ppu_, joypad_),
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

    void Board::unloadCartridge()
    {
        cartridge_ = Cartridge::Cartridge{};
        executionMode_ = ExecutionMode::DMG;
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
        buttons_ = 0;
        joypad_.reset();
        serial_.reset(executionMode_ == ExecutionMode::CGB);
        dma_.reset(model_ == Model::CGB);
        ppu_.reset(model_ == Model::CGB, executionMode_ == ExecutionMode::CGB);

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

    void Board::setButtons(std::uint8_t buttons)
    {
        // Acordar de STOP depende de uma nova pressão, mesmo fora do grupo selecionado em JOYP.
        if ((buttons & ~buttons_) != 0)
            cpu_.wakeFromStop();
        buttons_ = buttons;
        joypad_.setButtons(buttons, interrupts_);
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
            // A PPU observa no mesmo T-cycle a cópia e o estado ativo atualizados pelo DMA.
            dma_.tick(bus_);
            // Velocidade normal: um dot por T-cycle. Em double speed, a futura
            // divisão de clocks deve entregar um dot a cada dois T-cycles.
            ppu_.tick(memory_, interrupts_, dma_.active());
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

    void Board::state(StateArchive &archive)
    {
        archive(clockCycles_, buttons_, cartridge_, dma_, interrupts_, joypad_, serial_, timer_, memory_, ppu_, cpu_);
    }

    std::vector<std::uint8_t> Board::saveState()
    {
        if (!cartridge_.loaded())
            throw std::runtime_error("No ROM loaded.");
        std::vector<std::uint8_t> payload;
        StateArchive writer(payload);
        state(writer);
        std::vector<std::uint8_t> result;
        StateArchive header(result);
        std::string magic = "R2BOY-STATE";
        std::uint32_t version = 1;
        auto model = model_;
        auto mode = executionMode_;
        auto romHash = Common::RomFile::hash(cartridge_.rom());
        auto checksum = Common::RomFile::hash(payload);
        header(magic, version, model, mode, romHash, checksum);
        result.insert(result.end(), payload.begin(), payload.end());
        return result;
    }

    void Board::loadState(const std::vector<std::uint8_t> &data)
    {
        if (!cartridge_.loaded())
            throw std::runtime_error("No ROM loaded.");
        // Três comprimentos de texto, versão/modelo/modo e textos fixos de assinatura e hash.
        constexpr std::size_t headerSize = sizeof("R2BOY-STATE") - 1 + 8 + 8 + 6 * 4;
        if (data.size() < headerSize)
            throw std::runtime_error("Truncated save state header.");
        const std::vector<std::uint8_t> headerBytes(data.begin(), data.begin() + headerSize);
        StateArchive header(headerBytes);
        std::string magic, romHash, checksum;
        std::uint32_t version = 0;
        Model model{};
        ExecutionMode mode{};
        header(magic, version, model, mode, romHash, checksum);
        header.finish();
        if (magic != "R2BOY-STATE" || version != 1)
            throw std::runtime_error("Unsupported save state format/version.");
        if (model != model_ || mode != executionMode_ || romHash != Common::RomFile::hash(cartridge_.rom()))
            throw std::runtime_error("Save state belongs to a different ROM or hardware model.");
        const std::vector<std::uint8_t> payload(data.begin() + headerSize, data.end());
        if (checksum != Common::RomFile::hash(payload))
            throw std::runtime_error("Save state checksum mismatch.");
        Board next(model_);
        next.loadCartridge(cartridge_);
        StateArchive reader(payload);
        next.state(reader);
        reader.finish();
        // Aplica os componentes validados, preservando o barramento e os callbacks da CPU.
        clockCycles_ = next.clockCycles_;
        buttons_ = next.buttons_;
        cartridge_ = std::move(next.cartridge_);
        dma_ = std::move(next.dma_);
        interrupts_ = std::move(next.interrupts_);
        joypad_ = std::move(next.joypad_);
        serial_ = std::move(next.serial_);
        timer_ = std::move(next.timer_);
        memory_ = std::move(next.memory_);
        ppu_ = std::move(next.ppu_);
        cpu_.r_ = next.cpu_.r_;
        cpu_.ime_ = next.cpu_.ime_;
        cpu_.imeDelay_ = next.cpu_.imeDelay_;
        cpu_.halted_ = next.cpu_.halted_;
        cpu_.stopped_ = next.cpu_.stopped_;
        cpu_.haltBug_ = next.cpu_.haltBug_;
        cpu_.locked_ = next.cpu_.locked_;
        cpu_.cycles_ = next.cpu_.cycles_;
        cpu_.stepCycles_ = next.cpu_.stepCycles_;
    }
}