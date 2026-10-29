#pragma once
#include <string>
#include <string_view>

namespace R2BOY::Common
{
    inline bool validUtf8Path(std::string_view value)
    {
        for (std::size_t i = 0; i < value.size();)
        {
            const auto c = static_cast<unsigned char>(value[i]);
            if (c == 0)
                return false;
            if (c < 0x80)
            {
                ++i;
                continue;
            }
            unsigned length = c >= 0xC2 && c <= 0xDF ? 2 : c >= 0xE0 && c <= 0xEF ? 3
                                                       : c >= 0xF0 && c <= 0xF4   ? 4
                                                                                  : 0;
            if (!length || i + length > value.size())
                return false;
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((second & 0xC0) != 0x80 || (c == 0xE0 && second < 0xA0) ||
                (c == 0xED && second >= 0xA0) || (c == 0xF0 && second < 0x90) ||
                (c == 0xF4 && second >= 0x90))
                return false;
            for (unsigned j = 2; j < length; ++j)
                if ((static_cast<unsigned char>(value[i + j]) & 0xC0) != 0x80)
                    return false;
            i += length;
        }
        return true;
    }

    // Preserva UTF-8 válido e escapa bytes de controle e sequências inválidas.
    inline std::string escapeTerminal(std::string_view value, bool allowNewline = false)
    {
        constexpr char hex[] = "0123456789ABCDEF";
        std::string out;
        for (std::size_t i = 0; i < value.size();)
        {
            const auto c = static_cast<unsigned char>(value[i]);
            unsigned length = c >= 0xC2 && c <= 0xDF ? 2 : c >= 0xE0 && c <= 0xEF ? 3
                                                       : c >= 0xF0 && c <= 0xF4   ? 4
                                                                                  : 0;
            if (length && i + length <= value.size() && validUtf8Path(value.substr(i, length)) &&
                !(c == 0xC2 && static_cast<unsigned char>(value[i + 1]) < 0xA0))
            {
                out.append(value.substr(i, length));
                i += length;
                continue;
            }
            if ((c >= 0x20 && c < 0x7F) || (allowNewline && (c == '\n' || c == '\t')))
                out.push_back(static_cast<char>(c));
            else
            {
                out += "\\x";
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 15]);
            }
            ++i;
        }
        return out;
    }

    // ImGui interpreta ## como fim do rótulo; representa a sequência explicitamente.
    inline std::string safeLabel(std::string_view value)
    {
        auto out = escapeTerminal(value);
        std::size_t pos = 0;
        while ((pos = out.find("##", pos)) != std::string::npos)
        {
            out.replace(pos, 2, "#\\x23");
            pos += 5;
        }
        return out;
    }
}