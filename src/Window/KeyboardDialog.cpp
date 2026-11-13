#include "KeyboardDialog.h"
#include <imgui.h>
#include <string>

namespace R2BOY::Window
{
    void KeyboardDialog::setMapping(const System::KeyboardMapping &mapping)
    {
        mapping_ = mapping;
    }

    void KeyboardDialog::setInverted(bool inverted)
    {
        inverted_ = inverted;
    }

    void KeyboardDialog::open()
    {
        draft_ = mapping_;
        capture_ = -1;
        error_ = false;
        requestOpen_ = true;
    }

    bool KeyboardDialog::active() const
    {
        return requestOpen_ || open_;
    }

    bool KeyboardDialog::processEvent(const SDL_Event &event, SDL_WindowID window)
    {
        if (!active() || event.type != SDL_EVENT_KEY_DOWN || event.key.windowID != window || event.key.repeat)
            return false;
        if (capture_ >= 0)
        {
            if (System::assignableKey(event.key.key))
            {
                // Uma tecla não permanece associada a duas funções.
                for (auto &keycode : draft_)
                    if (keycode == event.key.key)
                        keycode = 0;
                draft_[capture_] = event.key.key;
                capture_ = -1;
            }
            return true;
        }
        return true;
    }

    void KeyboardDialog::draw(const Saver &save)
    {
        if (requestOpen_)
        {
            ImGui::OpenPopup("Configure Keyboard");
            requestOpen_ = false;
            open_ = true;
        }
        ImGui::SetNextWindowSize(ImVec2(310, 390), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Configure Keyboard", &open_, ImGuiWindowFlags_NoResize))
        {
            if (!open_)
                capture_ = -1;
            return;
        }
        ImGui::TextUnformatted("Button");
        ImGui::SameLine(120);
        ImGui::TextUnformatted("Mapping");
        ImGui::BeginChild("bindings", ImVec2(0, 250), ImGuiChildFlags_Borders);
        constexpr const char *names[] = {"Up", "Down", "Left", "Right", "B", "A", "Turbo B", "Turbo A", "Start", "Select"};
        for (int i = 0; i < 10; ++i)
        {
            ImGui::PushID(i);
            // Usa a associação correspondente à função exibida após a inversão.
            constexpr int sourceWhenInverted[] = {0, 1, 2, 3, 6, 4, 7, 5, 8, 9};
            const int source = inverted_ ? sourceWhenInverted[i] : i;
            ImGui::TextUnformatted(names[i]);
            ImGui::SameLine(120);
            const char *name = capture_ == source || !draft_[source] ? "" : SDL_GetKeyName(draft_[source]);
            if (ImGui::Button((std::string(name) + "###key").c_str(), ImVec2(-1, 0)))
                capture_ = source;
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::TextUnformatted(capture_ >= 0 ? "Press a key..." : "Select a mapping to change it.");
        ImGui::TextWrapped("Some emulator shortcuts and modifiers are reserved.");
        if (ImGui::Button("Cancel"))
        {
            open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Default"))
        {
            draft_ = System::defaultKeyboardMapping();
            capture_ = -1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            // Uma falha preserva o rascunho para que o usuário possa tentar novamente.
            if (save(draft_))
            {
                mapping_ = draft_;
                open_ = false;
                ImGui::CloseCurrentPopup();
            }
            else
                error_ = true;
        }
        if (error_)
            ImGui::TextWrapped("Could not save keyboard settings.");
        ImGui::EndPopup();
    }
}