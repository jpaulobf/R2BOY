#pragma once

namespace R2BOY::System
{
    class PauseState
    {
    public:
        bool paused() const { return manual_ || menu_ || minimized_; }
        bool manual() const { return manual_; }
        bool menu() const { return menu_; }
        void setManual(bool value) { manual_ = value; }
        void setMenu(bool value) { menu_ = value; }
        void setMinimized(bool value) { minimized_ = value; }

    private:
        bool manual_ = false, menu_ = false, minimized_ = false;
    };
}