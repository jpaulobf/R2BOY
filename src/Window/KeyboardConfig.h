#pragma once
#include "System/KeyboardMapping.h"

namespace R2BOY::Window
{
    class ConfigManager;
    // Lê e persiste as teclas, mantendo o formato do arquivo fora do diálogo.
    class KeyboardConfig
    {
    public:
        // O gerenciador deve permanecer válido durante a vida desta instância.
        explicit KeyboardConfig(ConfigManager &config) : config_(config) {}
        // Usa os padrões nas posições ausentes ou com valores inválidos.
        System::KeyboardMapping load() const;
        // Retorna false se não for possível concluir a gravação do mapeamento.
        bool save(const System::KeyboardMapping &mapping);

    private:
        ConfigManager &config_;
    };
}