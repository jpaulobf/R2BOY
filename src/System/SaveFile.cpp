#include "SaveFile.h"
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
    SaveFile::SaveFile(std::filesystem::path directory) : directory_(std::move(directory)) {}

    std::filesystem::path SaveFile::pathFor(const Core::Cartridge::Cartridge &cartridge) const
    {
        if (!cartridge.loaded())
            throw std::runtime_error("Cannot identify save for an empty cartridge.");
        return directory_ / (Common::RomFile::hash(cartridge.rom()) + ".sav");
    }

    bool SaveFile::load(Core::Cartridge::Cartridge &cartridge) const
    {
        if (!cartridge.hasBattery() || cartridge.ram().empty())
            return false;

        const auto path = pathFor(cartridge);
        if (!std::filesystem::exists(path))
            return false;
        std::ifstream file(path, std::ios::binary | std::ios::ate);

        if (!file)
            throw std::runtime_error("Unable to open save RAM: " + path.u8string());

        if (file.tellg() != static_cast<std::streamoff>(cartridge.ram().size()))
            throw std::runtime_error("Save RAM has an incompatible size: " + path.u8string());

        std::vector<std::uint8_t> data(cartridge.ram().size());
        file.seekg(0);

        if (!file.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size())))
            throw std::runtime_error("Unable to read complete save RAM: " + path.u8string());
        cartridge.restoreBatteryRam(std::move(data));

        return true;
    }

    bool SaveFile::save(Core::Cartridge::Cartridge &cartridge) const
    {
        if (!cartridge.hasBattery() || cartridge.ram().empty() || !cartridge.ramDirty())
            return false;

        std::filesystem::create_directories(directory_);
        const auto path = pathFor(cartridge);
        auto temporary = path;
        temporary += ".tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);

        if (!file)
            throw std::runtime_error("Unable to create save RAM: " + temporary.u8string());
        file.write(reinterpret_cast<const char *>(cartridge.ram().data()),
                   static_cast<std::streamsize>(cartridge.ram().size()));
        file.close();

        if (!file)
            throw std::runtime_error("Unable to write complete save RAM: " + temporary.u8string());
        // No Windows, rename não substitui um arquivo existente; usa a operação nativa.
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Unable to replace save RAM: " + path.u8string());
#else
        std::filesystem::rename(temporary, path);
#endif
        cartridge.markRamSaved();

        return true;
    }
}
