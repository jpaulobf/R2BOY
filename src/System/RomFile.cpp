#include "RomFile.h"
#include <miniz.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <iomanip>
#include <sstream>

namespace R2BOY::System
{
    namespace
    {
        // Limita a alocação tanto do arquivo de entrada quanto de um membro descomprimido.
        constexpr std::size_t maxRomSize = 8 * 1024 * 1024;
        constexpr std::size_t maxZipSize = 128 * 1024 * 1024;

        std::string extension(const std::filesystem::path &path)
        {
            auto ext = path.extension().u8string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return ext;
        }

        std::vector<std::uint8_t> readFile(const std::filesystem::path &path, std::size_t limit)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                throw std::runtime_error("Unable to open file.");
            const auto size = file.tellg();
            
            if (size <= 0 || static_cast<std::uintmax_t>(size) > limit)
                throw std::runtime_error("File is empty or exceeds the supported size limit.");
            std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
            file.seekg(0);

            if (!file.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size())))
                throw std::runtime_error("Unable to read the complete file.");
            return data;
        }

        // O miniz referencia data enquanto o leitor está aberto. O destrutor encerra o
        // leitor antes da liberação desse buffer, inclusive ao sair por exceção.
        struct Zip
        {
            std::vector<std::uint8_t> data;
            mz_zip_archive archive{};

            explicit Zip(const std::filesystem::path &path) : data(readFile(path, maxZipSize))
            {
                if (!mz_zip_reader_init_mem(&archive, data.data(), data.size(), 0))
                    throw std::runtime_error("Invalid or unsupported ZIP archive.");
            }

            ~Zip() { mz_zip_reader_end(&archive); }
            Zip(const Zip &) = delete;
            Zip &operator=(const Zip &) = delete;
        };
    }

    bool RomFile::isRom(const std::filesystem::path &path)
    {
        const auto ext = extension(path);
        return ext == ".gb" || ext == ".gbc";
    }

    bool RomFile::isZip(const std::filesystem::path &path) { return extension(path) == ".zip"; }

    std::string RomFile::hash(const std::vector<std::uint8_t> &rom)
    {
        const auto crc = mz_crc32(MZ_CRC32_INIT, rom.data(), rom.size());
        std::ostringstream result;
        result << std::uppercase << std::hex << std::setfill('0') << std::setw(8)
               << static_cast<std::uint32_t>(crc);
        return result.str();
    }

    std::vector<RomFile::Entry> RomFile::entries(const std::filesystem::path &path)
    {
        Zip zip(path);
        std::vector<Entry> result;

        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip.archive); ++i)
        {
            mz_zip_archive_file_stat stat{};
            if (!mz_zip_reader_file_stat(&zip.archive, i, &stat))
                throw std::runtime_error("Unable to read ZIP directory.");

            // Consulta o tamanho real: o nome abreviado em stat pode truncar caminhos longos.
            const auto length = mz_zip_reader_get_filename(&zip.archive, i, nullptr, 0);
            if (!length)
                throw std::runtime_error("Unable to read ZIP member name.");

            std::string name(length, '\0');
            mz_zip_reader_get_filename(&zip.archive, i, name.data(), length);
            name.resize(length - 1);
            if (!stat.m_is_directory && isRom(std::filesystem::u8path(name)))
                result.push_back({i, std::move(name)});
        }
        if (result.empty())
            throw std::runtime_error("ZIP contains no .gb or .gbc ROMs.");
        return result;
    }

    std::vector<std::uint8_t> RomFile::read(const std::filesystem::path &path)
    {
        if (!isRom(path))
            throw std::runtime_error("Choose a .gb or .gbc ROM.");
        return readFile(path, maxRomSize);
    }

    std::vector<std::uint8_t> RomFile::read(const std::filesystem::path &path, unsigned int entry)
    {
        Zip zip(path);
        mz_zip_archive_file_stat stat{};

        if (!mz_zip_reader_file_stat(&zip.archive, entry, &stat) || stat.m_is_directory ||
            stat.m_uncomp_size < 0x150 || stat.m_uncomp_size > maxRomSize)
            throw std::runtime_error("ZIP ROM has an invalid size (maximum 8 MiB).");
        if (stat.m_is_encrypted || !stat.m_is_supported)
            throw std::runtime_error("Encrypted or unsupported ZIP member.");

        // Valida o tamanho descomprimido antes da alocação; o tamanho do ZIP sozinho
        // não limita quanto um membro pode ocupar em memória.
        std::vector<std::uint8_t> rom(static_cast<std::size_t>(stat.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&zip.archive, entry, rom.data(), rom.size(), 0))
            throw std::runtime_error("Unable to extract ROM: corrupt ZIP data or CRC mismatch.");
        return rom;
    }
}
