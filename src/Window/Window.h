#pragma once

#include <memory>

class Window
{
public:
    Window();
    ~Window();

    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    bool open();
    void pollEvents(bool &running);
    bool isMinimized() const;
    bool renderFrame(bool &running);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
