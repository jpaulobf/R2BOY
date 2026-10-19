#include "Core/Cartridge/Cartridge.h"
#include "Common/RomFile.h"
#include <miniz.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace R2BOY::Test
{
    using Common::RomFile;
    using Core::Cartridge::Cartridge;
    using Core::Cartridge::Header;

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

    void checksums(std::vector<std::uint8_t> &rom)
    {
        std::uint8_t header = 0;
        for (int i = 0x134; i <= 0x14C; ++i)
            header = static_cast<std::uint8_t>(header - rom[i] - 1);
        rom[0x14D] = header;
        std::uint16_t sum = 0;
        for (std::size_t i = 0; i < rom.size(); ++i)
            if (i != 0x14E && i != 0x14F)
                sum = static_cast<std::uint16_t>(sum + rom[i]);
        rom[0x14E] = static_cast<std::uint8_t>(sum >> 8);
        rom[0x14F] = static_cast<std::uint8_t>(sum);
    }

    void write(const std::filesystem::path &path, const std::vector<std::uint8_t> &data)
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        check(static_cast<bool>(file), "write fixture");
    }

    struct Fixtures
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("r2boy-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Fixtures() { std::filesystem::create_directories(path); }
        ~Fixtures()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };

    int runCartridgeTests()
    {
        try
        {
            const std::vector<std::uint8_t> crcReference = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
            check(RomFile::hash(crcReference) == "CBF43926", "standard CRC32 reference");
            check(RomFile::hash({}) == "00000000", "CRC32 zero padding");
            std::vector<std::uint8_t> rom(32768);
            const std::string title = "SYNTHETIC";
            std::copy(title.begin(), title.end(), rom.begin() + 0x134);
            checksums(rom);
            auto header = Header::parse(rom);
            check(header.title == title && header.romSize == 32768, "DMG title and size");
            check(header.headerChecksumValid && header.globalChecksumValid && !header.logoValid, "checksum validation");
            rejects([]
                    { Header::parse(std::vector<std::uint8_t>(0x14F)); }, "short header accepted");
            auto color = rom;
            color[0x143] = 0xC0;
            std::copy_n("AB12", 4, color.begin() + 0x13F);
            color[0x147] = 0x06;
            header = Header::parse(color);
            check(header.manufacturer == "AB12" && header.title == title && header.ramSize == 512, "CGB/MBC2 metadata");
            check(std::string(header.compatibility()) == "Game Boy Color only", "CGB-only flag");
            color[0x143] = 0x80;
            check(std::string(Header::parse(color).compatibility()) == "Game Boy / Game Boy Color", "dual compatibility");
            for (std::uint8_t code = 0; code <= 8; ++code)
            {
                color[0x148] = code;
                check(Header::parse(color).romSize == (std::size_t{32768} << code), "ROM size code");
            }
            color[0x148] = 0x52;
            check(Header::parse(color).romSize == 72 * 16384, "legacy ROM size");
            Cartridge cartridge;
            cartridge.load(rom, "original");
            rejects([&]
                    { cartridge.load(std::vector<std::uint8_t>(100), "bad"); }, "invalid cartridge accepted");
            auto truncated = rom;
            truncated[0x148] = 1;
            rejects([&]
                    { cartridge.load(truncated, "truncated"); }, "truncated cartridge accepted");
            check(cartridge.source() == "original" && cartridge.rom() == rom, "failed load replaced cartridge");
            auto altered = rom;
            altered[0x134] ^= 1;
            cartridge.load(altered, "homebrew");
            check(!cartridge.header().headerChecksumValid, "checksum mismatch should be a warning");

            Fixtures fixtures;
            const auto raw = fixtures.path / std::filesystem::u8path("jogo-ação.GBC");
            write(raw, rom);
            check(RomFile::read(raw) == rom, "Unicode raw ROM roundtrip");
            check(RomFile::isRom("game.GB") && RomFile::isZip("game.ZIP"), "case insensitive extensions");
            rejects([&]
                    { RomFile::read(fixtures.path / "missing.gb"); }, "missing file accepted");
            rejects([&]
                    { RomFile::read(fixtures.path / "game.txt"); }, "wrong extension accepted");
            write(fixtures.path / "bad.zip", rom);
            rejects([&]
                    { RomFile::entries(fixtures.path / "bad.zip"); }, "invalid ZIP accepted");

            mz_zip_archive zip{};
            check(mz_zip_writer_init_heap(&zip, 0, 0) != 0, "init ZIP fixture");
            check(mz_zip_writer_add_mem(&zip, "readme.txt", "test", 4, MZ_DEFAULT_COMPRESSION) != 0, "add ignored file");
            check(mz_zip_writer_add_mem(&zip, "nested/one.GB", rom.data(), rom.size(), MZ_DEFAULT_COMPRESSION) != 0, "add deflated ROM");
            check(mz_zip_writer_add_mem(&zip, "two.gbc", color.data(), color.size(), 0) != 0, "add stored ROM");
            void *buffer = nullptr;
            size_t length = 0;
            check(mz_zip_writer_finalize_heap_archive(&zip, &buffer, &length) != 0, "finalize ZIP");
            const auto *bytes = static_cast<const std::uint8_t *>(buffer);
            std::vector<std::uint8_t> archive(bytes, bytes + length);
            mz_free(buffer);
            mz_zip_writer_end(&zip);
            const auto zipPath = fixtures.path / "games.zip";
            write(zipPath, archive);
            auto entries = RomFile::entries(zipPath);
            check(entries.size() == 2 && entries[0].name == "nested/one.GB", "ZIP ROM filtering");
            check(RomFile::read(zipPath, entries[0].index) == rom, "deflated ZIP roundtrip");
            check(RomFile::hash(RomFile::read(zipPath, entries[0].index)) == RomFile::hash(RomFile::read(raw)),
                  "raw and ZIP ROM hashes differ");
            check(RomFile::read(zipPath, entries[1].index) == color, "stored ZIP roundtrip");
            rejects([&]
                    { RomFile::read(zipPath, 999); }, "invalid ZIP index accepted");
            // Corrupt the first ROM's central-directory CRC.
            for (std::size_t i = 0; i + 59 <= archive.size(); ++i)
            {
                if (archive[i] == 'P' && archive[i + 1] == 'K' && archive[i + 2] == 1 && archive[i + 3] == 2 &&
                    archive[i + 28] == 13 && std::equal(archive.begin() + i + 46, archive.begin() + i + 59, "nested/one.GB"))
                    archive[i + 16] ^= 1;
            }
            write(fixtures.path / "crc.zip", archive);
            rejects([&]
                    { RomFile::read(fixtures.path / "crc.zip", entries[0].index); }, "bad ZIP CRC accepted");
            archive.resize(archive.size() / 2);
            write(fixtures.path / "truncated.zip", archive);
            rejects([&]
                    { RomFile::entries(fixtures.path / "truncated.zip"); }, "truncated ZIP accepted");
            std::cout << "Cartridge and ROM file tests passed.\n";
            return 0;
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
} // namespace R2BOY::Test

int main()
{
    return R2BOY::Test::runCartridgeTests();
}
