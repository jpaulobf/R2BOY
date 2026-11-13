#include "KeyboardConfig.h"
#include "ConfigManager.h"
#include <array>
#include <charconv>
#include <string>

namespace R2BOY::Window
{
    namespace
    {
        std::string key(int button) { return "keyboard_1_" + std::to_string(button); }
    }
    System::KeyboardMapping KeyboardConfig::load() const
    {
        // Valores ausentes ou inválidos conservam o padrão de cada posição.
        auto mapping = System::defaultKeyboardMapping();
        for (int b = 0; b < 10; ++b)
        {
            const auto value = config_.getValue(key(b));
            unsigned parsed = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (!value.empty() && result.ec == std::errc{} && result.ptr == value.data() + value.size() &&
                (parsed == 0 || System::assignableKey(parsed)))
                mapping[b] = parsed;
        }
        return mapping;
    }
    bool KeyboardConfig::save(const System::KeyboardMapping &mapping)
    {
        std::array<std::string, 10> previous;
        for (int b = 0; b < 10; ++b)
        {
            previous[b] = config_.getValue(key(b));
            config_.setValue(key(b), std::to_string(mapping[b]));
        }
        // Restaura os valores em memória se a gravação não for concluída.
        if (config_.saveConfigFile())
            return true;
        for (int b = 0; b < 10; ++b)
            config_.setValue(key(b), previous[b]);
        return false;
    }
}