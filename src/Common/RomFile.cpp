#include "RomFile.h"
#include <miniz.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <array>

namespace R2BOY::Common
{
    namespace
    {
        // Limita a alocação tanto do arquivo de entrada quanto de um membro descomprimido.
        constexpr std::size_t maxRomSize = 8 * 1024 * 1024;
        constexpr std::size_t maxZipSize = 128 * 1024 * 1024;
        // Evita que um diretório central gigantesco force uma enumeração/alocação excessiva.
        constexpr mz_uint maxZipEntries = 65536;
        constexpr mz_uint maxZipNameLength = 4096;

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

            // Mantém a imagem do ZIP viva durante toda a sessão de leitura do miniz.
            explicit Zip(const std::filesystem::path &path) : data(readFile(path, maxZipSize))
            {
                if (!mz_zip_reader_init_mem(&archive, data.data(), data.size(), 0))
                    throw std::runtime_error("Invalid or unsupported ZIP archive.");
            }

            // Libera os recursos internos do leitor antes da destruição de data.
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

    std::string RomFile::identity(const std::vector<std::uint8_t> &data)
    {
        constexpr std::array<std::uint32_t, 64> k{
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::array<std::uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        auto input = data;
        const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
        input.push_back(0x80);
        while (input.size() % 64 != 56)
            input.push_back(0);
        for (int i = 7; i >= 0; --i)
            input.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
        const auto rotr = [](std::uint32_t x, unsigned n)
        { return (x >> n) | (x << (32 - n)); };
        for (std::size_t block = 0; block < input.size(); block += 64)
        {
            std::array<std::uint32_t, 64> w{};
            for (unsigned i = 0; i < 16; ++i)
                for (unsigned j = 0; j < 4; ++j)
                    w[i] = (w[i] << 8) | input[block + i * 4 + j];
            for (unsigned i = 16; i < 64; ++i)
                w[i] = w[i - 16] + (rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] +
                       (rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10));
            auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], z = h[7];
            for (unsigned i = 0; i < 64; ++i)
            {
                const auto t1 = z + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
                const auto t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
                z = g;
                g = f;
                f = e;
                e = d + t1;
                d = c;
                c = b;
                b = a;
                a = t1 + t2;
            }
            h[0] += a;
            h[1] += b;
            h[2] += c;
            h[3] += d;
            h[4] += e;
            h[5] += f;
            h[6] += g;
            h[7] += z;
        }
        std::ostringstream out;
        out << std::uppercase << std::hex << std::setfill('0');
        for (const auto value : h)
            out << std::setw(8) << value;
        return out.str();
    }

    std::vector<RomFile::Entry> RomFile::entries(const std::filesystem::path &path)
    {
        Zip zip(path);
        std::vector<Entry> result;

        if (mz_zip_reader_get_num_files(&zip.archive) > maxZipEntries)
            throw std::runtime_error("ZIP contains too many entries.");

        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip.archive); ++i)
        {
            mz_zip_archive_file_stat stat{};
            if (!mz_zip_reader_file_stat(&zip.archive, i, &stat))
                throw std::runtime_error("Unable to read ZIP directory.");

            // Consulta o tamanho real: o nome abreviado em stat pode truncar caminhos longos.
            const auto length = mz_zip_reader_get_filename(&zip.archive, i, nullptr, 0);
            if (!length || length > maxZipNameLength)
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