#include "Cartridge.h"

#include <stdexcept>
#include <utility>

namespace R2BOY::Core::Cartridge
{
    void Cartridge::load(std::vector<std::uint8_t> rom, std::string source)
    {
        Header header = Header::parse(rom);

        if (rom.size() < 32768)
            throw std::runtime_error("ROM is smaller than the minimum 32 KiB cartridge.");

        if (header.romSize != 0 && rom.size() < header.romSize)
            throw std::runtime_error("ROM is truncated: smaller than the size declared in its header.");

        // Só substitui o estado após validar a imagem, preservando a ROM anterior em caso de erro.
        header_ = std::move(header);
        rom_ = std::move(rom);
        source_ = std::move(source);
    }
}
