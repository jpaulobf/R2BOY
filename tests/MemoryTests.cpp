#include "Core/Board.h"
#include "System/SaveFile.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace R2BOY::Test
{
    namespace
    {
        void check(bool condition, const char *message)
        {
            if (!condition)
                throw std::runtime_error(message);
        }

        template <class F>
        void rejects(F action, const char *message)
        {
            try
            {
                action();
            }
            catch (const std::exception &)
            {
                return;
            }
            throw std::runtime_error(message);
        }

        // Cada banco contém sua identificação em dois bytes, inclusive acima de 255.
        std::vector<std::uint8_t> rom(std::uint8_t type, unsigned banks = 2, std::uint8_t ramCode = 0)
        {
            std::vector<std::uint8_t> data(banks * 16384);
            for (unsigned bank = 0; bank < banks; ++bank)
            {
                std::fill_n(data.begin() + bank * 16384, 16384, static_cast<std::uint8_t>(bank));
                data[bank * 16384 + 1] = static_cast<std::uint8_t>(bank >> 8);
            }
            data[0x147] = type;
            data[0x148] = 0;
            for (unsigned count = 2; count < banks; count *= 2)
                ++data[0x148];
            data[0x149] = ramCode;
            return data;
        }

        void cartridgeBanks()
        {
            Core::Cartridge::Cartridge cart;
            check(cart.read(0) == 0xFF && cart.read(0xA000) == 0xFF, "empty cartridge");
            cart.load(rom(0), "ROM only");
            check(cart.read(0x4000) == 1 && cart.ram().empty(), "ROM-only banks");
            cart.write(0, 0x0A);
            cart.write(0x2000, 7);
            check(cart.read(0x4000) == 1 && cart.read(0) == 0, "ROM must remain immutable");
            cart.load(rom(8, 2, 1), "2 KiB RAM");
            cart.write(0xA000, 0x12);
            check(cart.read(0xA800) == 0x12 && cart.read(0xB800) == 0x12, "2 KiB RAM mirror");

            cart.load(rom(3, 32, 3), "MBC1 RAM");
            cart.write(0xA000, 0x11);
            check(cart.read(0xA000) == 0xFF && !cart.ramDirty(), "RAM gate at power-on");
            cart.write(0, 0x1A);
            cart.write(0xA000, 0x11);
            cart.write(0x4000, 2);
            check(cart.read(0xA000) == 0x11, "MBC1 mode 0 RAM bank fixed");
            cart.write(0x6000, 1);
            cart.write(0xA000, 0x22);
            cart.write(0x4000, 0);
            check(cart.read(0xA000) == 0x11, "MBC1 RAM bank isolation");
            cart.write(0x4000, 2);
            check(cart.read(0xA000) == 0x22, "MBC1 selected RAM bank");
            cart.resetBanks();
            check(cart.read(0xA000) == 0xFF, "reset disables cartridge RAM");
            cart.write(0, 0x0A);
            check(cart.read(0xA000) == 0x11, "reset preserves cartridge RAM");

            cart.load(rom(1, 128), "MBC1 ROM");
            cart.write(0x2000, 0);
            cart.write(0x4000, 1);
            check(cart.read(0x4000) == 33 && cart.read(0) == 0, "MBC1 forbidden bank correction");
            cart.write(0x6000, 1);
            check(cart.read(0) == 32 && cart.read(0x4000) == 33, "MBC1 advanced lower ROM window");
            cart.load(rom(1, 16), "MBC1 small ROM");
            cart.write(0x2000, 0x10);
            check(cart.read(0x4000) == 0, "MBC1 correction precedes address masking");

            cart.load(rom(6, 16), "MBC2");
            check(cart.ram().size() == 512 && cart.hasBattery(), "MBC2 internal RAM");
            cart.write(0x0100, 3);
            check(cart.read(0x4000) == 3 && cart.read(0xA000) == 0xFF, "MBC2 A8 ROM select");
            cart.write(0x2000, 0x0A);
            cart.write(0xA001, 0xAB);
            check(cart.read(0xA201) == 0xFB && cart.read(0xBE01) == 0xFB, "MBC2 nibble RAM mirrors");
            cart.write(0x2100, 0);
            check(cart.read(0x4000) == 1, "MBC2 bank zero correction");
            cart.write(0, 0);
            cart.write(0xA001, 3);
            cart.write(0, 0x0A);
            check(cart.read(0xA001) == 0xFB, "disabled MBC2 writes ignored");

            cart.load(rom(0x13, 128, 3), "MBC3");
            cart.write(0x2000, 0x20);
            check(cart.read(0x4000) == 32, "MBC3 bank 32 supported");
            cart.write(0, 0x0A);
            cart.write(0x4000, 3);
            cart.write(0xA000, 0x66);
            cart.write(0x4000, 8);
            cart.write(0xA000, 0x77);
            check(cart.read(0xA000) == 0xFF, "MBC3 missing RTC must not alias RAM");
            cart.write(0x4000, 3);
            check(cart.read(0xA000) == 0x66, "MBC3 RAM bank isolation");

            cart.load(rom(0x1B, 512, 4), "MBC5");
            cart.write(0x2000, 0);
            check(cart.read(0x4000) == 0 && cart.read(0x4001) == 0, "MBC5 real bank zero");
            cart.write(0x3000, 1);
            check(cart.read(0x4000) == 0 && cart.read(0x4001) == 1, "MBC5 ninth ROM bank bit");
            cart.write(0, 0x0A);
            cart.write(0x4000, 15);
            cart.write(0xBFFF, 0x55);
            cart.write(0x4000, 0);
            check(cart.read(0xBFFF) == 0xFF, "MBC5 RAM banks isolated");
            cart.write(0x4000, 15);
            check(cart.read(0xBFFF) == 0x55, "MBC5 last RAM bank byte");
            cart.load(rom(0x1E, 2, 3), "rumble");
            cart.write(0, 0x0A);
            cart.write(0xA000, 0x44);
            cart.write(0x4000, 8);
            check(cart.controller().rumbleActive() && cart.read(0xA000) == 0x44, "rumble bit is not RAM bank bit");

            rejects([&]
                    { cart.load(rom(0x10), "RTC"); }, "RTC cartridge silently accepted");
            rejects([&]
                    { cart.load(rom(0xFC), "camera"); }, "unsupported controller accepted");
            check(cart.source() == "rumble", "unsupported load replaced current cartridge");
            rejects([&]
                    { cart.load(rom(0, 4), "too large"); }, "ROM-only oversized image accepted");
            rejects([&]
                    { cart.load(rom(3, 128, 3), "invalid MBC1 wiring"); }, "large MBC1 with banked RAM accepted");
            auto unknownSize = rom(3, 2, 2);
            unknownSize[0x149] = 6;
            rejects([&]
                    { cart.load(unknownSize, "unknown RAM size"); }, "unknown RAM size accepted");
            cart.load(rom(1, 2, 3), "no RAM hardware");
            check(cart.ram().empty(), "RAM allocated without RAM hardware");

            const std::uint8_t logo[] = {
                0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
                0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
                0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63,
                0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E};
            auto multicart = rom(1, 64);
            std::copy(std::begin(logo), std::end(logo), multicart.begin() + 0x104);
            // Um único logo é um cartucho comum; um segundo no banco 0x10 indica MBC1M.
            cart.load(multicart, "standard MBC1");
            std::copy(std::begin(logo), std::end(logo), multicart.begin() + 16 * 16384 + 0x104);
            rejects([&]
                    { cart.load(multicart, "MBC1M"); }, "MBC1M mapped as standard MBC1");
        }

        void memoryMap()
        {
            Core::Board board(Core::Model::CGB);
            auto image = rom(3, 4, 2);
            image[0x143] = 0x80;
            board.loadCartridge(image, "color");
            check(board.executionMode() == Core::ExecutionMode::CGB, "CGB mode selection");
            board.write(0x2000, 2);
            check(board.read(0x4000) == 2, "BUS routes MBC writes");
            board.write(0, 0x0A);
            board.write(0xA000, 0x99);
            check(board.read(0xA000) == 0x99, "BUS routes cartridge RAM");
            board.write(0xC000, 0x11);
            check(board.read(0xE000) == 0x11, "WRAM echo forward");
            board.write(0xFDFF, 0x22);
            check(board.read(0xDDFF) == 0x22, "WRAM echo reverse");
            board.write(0xDE00, 0x33);
            check(board.read(0xFE00) == 0, "WRAM echo ends at FDFF");
            board.write(0xFE00, 0x44);
            board.write(0xFE9F, 0x55);
            board.write(0xFEA0, 0x66);
            check(board.read(0xFE00) == 0x44 && board.read(0xFE9F) == 0x55 && board.read(0xFEA0) == 0xFF, "OAM boundaries");
            board.write(0xFF80, 0x77);
            board.write(0xFFFE, 0x88);
            check(board.read(0xFF80) == 0x77 && board.read(0xFFFE) == 0x88, "HRAM boundaries");
            check(board.read(0xFF00) == 0xFF && board.read(0xFFFF) == 0xFF, "unimplemented IO open bus");
            board.write(0x8000, 0x10);
            board.write(0xFF4F, 1);
            board.write(0x8000, 0x20);
            board.write(0xFF4F, 0);
            check(board.read(0x8000) == 0x10 && board.read(0xFF4F) == 0xFE, "CGB VRAM banks");
            board.write(0xD000, 0x30);
            board.write(0xFF70, 2);
            board.write(0xD000, 0x40);
            check(board.read(0xF000) == 0x40, "CGB echo follows bank");
            board.write(0xFF70, 0);
            check(board.read(0xD000) == 0x30 && board.read(0xFF70) == 0xF8, "CGB WRAM zero aliases bank one");
            board.resetMemory();
            board.write(0, 0x0A);
            check(board.read(0x8000) == 0 && board.read(0xC000) == 0 && board.read(0xA000) == 0x99, "volatile reset preserves SRAM");
            auto dmg = rom(0);
            board.loadCartridge(dmg, "DMG");
            check(board.executionMode() == Core::ExecutionMode::DMG, "CGB runs DMG cartridge in DMG mode");
            board.write(0x8000, 0xAA);
            board.write(0xFF4F, 1);
            check(board.read(0x8000) == 0xAA && board.read(0xFF4F) == 0xFF, "DMG ignores CGB bank registers");
            Core::Board dmgBoard;
            dmgBoard.loadCartridge(dmg, "original");
            dmgBoard.write(0xC000, 0xBB);
            image[0x143] = 0xC0;
            rejects([&]
                    { dmgBoard.loadCartridge(image, "color only"); }, "DMG accepted CGB-only game");
            check(dmgBoard.cartridge().source() == "original" && dmgBoard.read(0xC000) == 0xBB, "failed insertion changed board");
        }

        void batterySaves()
        {
            const auto root = std::filesystem::temp_directory_path() /
                              ("r2boy-memory-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            struct Cleanup
            {
                std::filesystem::path path;
                ~Cleanup()
                {
                    std::error_code ec;
                    std::filesystem::remove_all(path, ec);
                }
            } cleanup{root};
            System::SaveFile saves(root / "saves");
            Core::Cartridge::Cartridge cart;
            const auto image = rom(3, 2, 2);
            cart.load(image, "raw.gb");
            check(!saves.load(cart) && !saves.save(cart), "missing/unchanged save");
            cart.write(0, 0x0A);
            cart.write(0xA000, 0x11);
            check(saves.save(cart) && !cart.ramDirty(), "save dirty battery RAM");
            check(std::filesystem::file_size(saves.pathFor(cart)) == 8192, "raw SRAM file size");
            cart.write(0xA000, 0x22);
            check(saves.save(cart), "replace existing save");
            Core::Cartridge::Cartridge restored;
            restored.load(image, "archive.zip :: game.gb");
            check(saves.pathFor(cart) == saves.pathFor(restored) && saves.load(restored), "save identity independent of container");
            restored.write(0, 0x0A);
            check(restored.read(0xA000) == 0x22 && !restored.ramDirty(), "restore SRAM content");
            {
                std::ofstream file(saves.pathFor(restored), std::ios::binary | std::ios::trunc);
                file.put('x');
            }
            rejects([&]
                    { saves.load(restored); }, "wrong size save accepted");
            check(restored.read(0xA000) == 0x22, "invalid save changed SRAM");
            cart.load(rom(2, 2, 2), "volatile");
            cart.write(0, 0x0A);
            cart.write(0xA000, 0x33);
            check(!saves.save(cart) && !saves.load(cart), "non-battery RAM persisted");
            cart.load(rom(6), "MBC2");
            cart.write(0, 0x0A);
            cart.write(0xA000, 0xA5);
            check(saves.save(cart) && std::filesystem::file_size(saves.pathFor(cart)) == 512, "MBC2 save cell format");
            restored.load(rom(6), "MBC2 restored");
            check(saves.load(restored), "MBC2 restore");
            restored.write(0, 0x0A);
            check(restored.read(0xA000) == 0xF5, "MBC2 restored nibble");
            const auto blocker = root / "not-a-directory";
            {
                std::ofstream file(blocker);
                file.put('x');
            }
            System::SaveFile blocked(blocker / "saves");
            restored.write(0xA000, 4);
            rejects([&]
                    { blocked.save(restored); }, "save into file parent accepted");
            check(restored.ramDirty(), "failed save cleared dirty flag");
        }
    }

    int runMemoryTests()
    {
        try
        {
            cartridgeBanks();
            memoryMap();
            batterySaves();
            std::cout << "Memory, MBC and battery-save tests passed.\n";
            return 0;
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
}

int main() { return R2BOY::Test::runMemoryTests(); }
