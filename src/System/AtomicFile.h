#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace R2BOY::System
{
    // Cria um arquivo novo e exclusivo no mesmo diretório, depois substitui o destino.
    void writeAtomicFile(const std::filesystem::path &path, const std::vector<std::uint8_t> &data);
}