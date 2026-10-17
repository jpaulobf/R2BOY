#pragma once

namespace R2BOY::Core::Cartridge::MBC
{
    /**
     * Reserva a abstração dos controladores de bancos do cartucho (MBC).
     * Esses controladores selecionam bancos de ROM/RAM e podem oferecer recursos
     * como relógio e vibração, conforme o tipo indicado no header. A interface será
     * definida junto ao primeiro controlador implementado. A classe está vazia e
     * ainda não participa dos acessos ao cartucho.
     */
    class MBC
    {
    };
}
