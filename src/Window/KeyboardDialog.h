#pragma once
#include "System/KeyboardMapping.h"
#include <functional>

namespace R2BOY::Window
{
    // Mantém o rascunho da edição e desenha o diálogo no contexto ImGui atual.
    class KeyboardDialog
    {
    public:
        // Recebe o rascunho; false mantém o diálogo aberto para nova tentativa.
        using Saver = std::function<bool(const System::KeyboardMapping &)>;
        // Define o mapeamento confirmado que será usado na próxima abertura.
        void setMapping(const System::KeyboardMapping &mapping);
        // Faz as linhas refletirem a inversão de botões ativa.
        void setInverted(bool inverted);
        // Abre uma cópia editável sem alterar o mapeamento confirmado.
        void open();
        // Considera tanto a abertura solicitada quanto o diálogo já apresentado.
        bool active() const;
        // Captura pressões da janela indicada e retorna se o evento foi consumido.
        bool processEvent(const SDL_Event &event, SDL_WindowID window);
        // Desenha o editor; Cancel descarta o rascunho e Save chama o callback.
        void draw(const Saver &save);

    private:
        // A cópia confirmada só é substituída depois de um salvamento bem-sucedido.
        System::KeyboardMapping mapping_ = System::defaultKeyboardMapping(), draft_ = mapping_;
        // capture_ identifica a função aguardando uma tecla; -1 indica captura inativa.
        int capture_ = -1;
        bool requestOpen_ = false, open_ = false, inverted_ = false, error_ = false;
    };
}