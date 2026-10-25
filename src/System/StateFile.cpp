#include "StateFile.h"
#include "Common/RomFile.h"
#include <fstream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace R2BOY::System
{
    StateFile::StateFile(std::filesystem::path directory) : directory(std::move(directory)) {}

    std::filesystem::path StateFile::pathFor(const Core::Board &board, int slot) const
    {
        if (slot < 1 || slot > 3)
            throw std::runtime_error("Invalid save state slot.");
        if (!board.cartridge().loaded())
            throw std::runtime_error("No ROM loaded.");
        return directory / (Common::RomFile::hash(board.cartridge().rom()) + ".slot" + std::to_string(slot) + ".state");
    }

    void StateFile::save(Core::Board &board, int slot) const
    {
        const auto path = pathFor(board, slot);
        auto data = board.saveState();
        std::filesystem::create_directories(directory);
        auto temporary = path;
        temporary += ".tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
            throw std::runtime_error("Unable to create save state: " + temporary.u8string());
        file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        file.close();
        if (!file)
            throw std::runtime_error("Unable to write save state: " + temporary.u8string());
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Unable to replace save state: " + path.u8string());
#else
        std::filesystem::rename(temporary, path);
#endif
    }

    void StateFile::load(Core::Board &board, int slot) const
    {
        const auto path = pathFor(board, slot);
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            throw std::runtime_error("No readable save state in slot " + std::to_string(slot) + ".");
        const auto size = file.tellg();
        if (size < 1 || size > 2 * 1024 * 1024)
            throw std::runtime_error("Invalid save state size.");
        std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size())))
            throw std::runtime_error("Unable to read save state.");
        board.loadState(data);
    }
}