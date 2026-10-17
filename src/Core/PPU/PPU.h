#pragma once

namespace R2BOY::Core::PPU
{
    /**
     * Reserva a unidade de vídeo, seus registradores e a geração de pixels.
     * Produzirá o framebuffer conforme a temporização do console. A apresentação
     * na janela fica na camada Window, mantendo o Core independente de SDL/ImGui.
     * A classe ainda é um esqueleto, sem renderização emulada.
     */
    class PPU
    {
    };
}
