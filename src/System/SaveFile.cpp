#include "SaveFile.h"
#include "AtomicFile.h"
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
    SaveFile::SaveFile(std::filesystem::path directory) : directory(std::move(directory)) {}

    std::filesystem::path SaveFile::pathFor(const Core::Cartridge::Cartridge &cartridge) const
    {
        if (!cartridge.loaded())
            throw std::runtime_error("Cannot identify save for an empty cartridge.");
        return directory / (cartridge.identity() + ".sav");
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
        // Aplica somente depois da leitura completa, preservando a RAM em caso de falha.
        cartridge.restoreBatteryRam(std::move(data));

        return true;
    }

    bool SaveFile::save(Core::Cartridge::Cartridge &cartridge) const
    {
        if (!cartridge.hasBattery() || cartridge.ram().empty() || !cartridge.ramDirty())
            return false;

        const auto path = pathFor(cartridge);
        writeAtomicFile(path, cartridge.ram());
        cartridge.markRamSaved();

        return true;
    }
}