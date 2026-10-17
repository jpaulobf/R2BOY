cmake_minimum_required(VERSION 3.14)
project(R2BOY VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
if(WIN32)
    # Avoid the ccache hangs observed with the Windows toolchain.
    set(SDL_CCACHE OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)
add_library(r2boy_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(r2boy_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(r2boy_imgui PUBLIC SDL2::SDL2-static)

add_executable(R2BOY
    src/Common/Constants.h
    src/Common/Types.h
    src/Core/APU/APU.cpp
    src/Core/APU/APU.h
    src/Core/BUS/BUS.cpp
    src/Core/BUS/BUS.h
    src/Core/Board.cpp
    src/Core/Board.h
    src/Core/CPU/CPU.cpp
    src/Core/CPU/CPU.h
    src/Core/Cartridge/Cartridge.cpp
    src/Core/Cartridge/Cartridge.h
    src/Core/Cartridge/Header.cpp
    src/Core/Cartridge/Header.h
    src/Core/Cartridge/MBC/MBC.cpp
    src/Core/Cartridge/MBC/MBC.h
    src/Core/IO/DMA.cpp
    src/Core/IO/DMA.h
    src/Core/IO/Interrupts.cpp
    src/Core/IO/Interrupts.h
    src/Core/IO/Joypad.cpp
    src/Core/IO/Joypad.h
    src/Core/IO/Serial.cpp
    src/Core/IO/Serial.h
    src/Core/IO/Timer.cpp
    src/Core/IO/Timer.h
    src/Core/Memory/Memory.cpp
    src/Core/Memory/Memory.h
    src/Core/Model.h
    src/Core/PPU/PPU.cpp
    src/Core/PPU/PPU.h
    src/Emulator.cpp
    src/Emulator.h
    src/System/Audio.cpp
    src/System/Audio.h
    src/System/Engine.cpp
    src/System/Engine.h
    src/System/Input.cpp
    src/System/Input.h
    src/Window/Window.cpp
    src/Window/Window.h
)
target_compile_features(R2BOY PRIVATE cxx_std_17)
set_target_properties(R2BOY PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(R2BOY PRIVATE src)
target_link_libraries(R2BOY PRIVATE SDL2::SDL2main r2boy_imgui)
if(MSVC)
    target_compile_options(R2BOY PRIVATE /W4)
else()
    target_compile_options(R2BOY PRIVATE -Wall -Wextra -Wpedantic)
endif()
