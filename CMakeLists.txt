cmake_minimum_required(VERSION 3.16)
project(R2BOY VERSION 0.0.2 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
if(WIN32)
    # Avoid the ccache hangs observed with the Windows toolchain.
    set(SDL_CCACHE OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    # Commit fixado (release-3.4.18): uma tag movida não altera o código baixado.
    GIT_TAG 829a65d769d935c4852f8159e964312c0957260a
)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    # Commit fixado (v1.92.9b): uma tag movida não altera o código baixado.
    GIT_TAG f1cc2ae15e53a861a874c3034aae6798fde194ab
)
FetchContent_MakeAvailable(imgui)
add_library(r2boy_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(r2boy_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(r2boy_imgui PUBLIC SDL3::SDL3-static)

FetchContent_Declare(miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    # Commit fixado (3.1.0): uma tag movida não altera o código baixado.
    GIT_TAG 174573d60290f447c13a2b1b3405de2b96e27d6c
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(miniz)

# Usamos a API mz_*; dispensa os aliases zlib que geram avisos de funções não utilizadas.
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

add_library(r2boy_common STATIC
    src/Common/Constants.h
    src/Common/Framebuffer.h
    src/Common/RomFile.cpp
    src/Common/RomFile.h
)
target_compile_features(r2boy_common PUBLIC cxx_std_17)
target_include_directories(r2boy_common PUBLIC src)
target_link_libraries(r2boy_common PRIVATE miniz)

# Descobre os arquivos do projeto e verifica inclusões/remoções a cada build.
file(GLOB_RECURSE R2BOY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)

# O núcleo é compartilhado com os testes e deve ser compilado apenas na biblioteca.
file(GLOB_RECURSE R2BOY_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/*.hpp"
)
list(APPEND R2BOY_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/System/SaveFile.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/System/SaveFile.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/System/StateFile.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/System/StateFile.h"
)
list(REMOVE_ITEM R2BOY_SOURCES ${R2BOY_CORE_SOURCES})
list(FILTER R2BOY_SOURCES EXCLUDE REGEX "/src/Common/")

add_library(r2boy_core STATIC ${R2BOY_CORE_SOURCES})
target_compile_features(r2boy_core PUBLIC cxx_std_17)
target_include_directories(r2boy_core PUBLIC src)
target_link_libraries(r2boy_core PUBLIC r2boy_common)

add_executable(R2BOY ${R2BOY_SOURCES})
if(WIN32)
    enable_language(RC)
    target_sources(R2BOY PRIVATE resources/resource.rc)
    target_include_directories(R2BOY PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    set_source_files_properties(resources/resource.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/logob.ico")
endif()
target_compile_features(R2BOY PRIVATE cxx_std_17)
set_target_properties(R2BOY PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(R2BOY PRIVATE src)
target_link_libraries(R2BOY PRIVATE r2boy_imgui r2boy_core)
if(MSVC)
    target_compile_options(R2BOY PRIVATE /W4)
    target_compile_options(r2boy_core PRIVATE /W4)
    target_compile_options(r2boy_common PRIVATE /W4)
else()
    target_compile_options(R2BOY PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(r2boy_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(r2boy_common PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(config_manager_tests tests/ConfigManagerTests.cpp src/Window/ConfigManager.cpp)
    target_compile_features(config_manager_tests PRIVATE cxx_std_17)
    target_include_directories(config_manager_tests PRIVATE src)
    add_test(NAME config_manager_tests COMMAND config_manager_tests)
    add_executable(apu_tests tests/APUTests.cpp)
    target_link_libraries(apu_tests PRIVATE r2boy_core)
    add_test(NAME apu_tests COMMAND apu_tests)
    add_executable(frame_pacer_tests tests/FramePacerTests.cpp)
    target_compile_features(frame_pacer_tests PRIVATE cxx_std_17)
    target_include_directories(frame_pacer_tests PRIVATE src)
    add_test(NAME frame_pacer_tests COMMAND frame_pacer_tests)
    add_executable(cartridge_tests tests/CartridgeTests.cpp)
    target_link_libraries(cartridge_tests PRIVATE r2boy_core miniz)
    add_test(NAME cartridge_tests COMMAND cartridge_tests)
    add_executable(state_tests tests/StateTests.cpp)
    target_link_libraries(state_tests PRIVATE r2boy_core)
    add_test(NAME state_tests COMMAND state_tests)
    add_executable(rewind_tests tests/RewindTests.cpp)
    target_link_libraries(rewind_tests PRIVATE r2boy_core)
    add_test(NAME rewind_tests COMMAND rewind_tests)
    add_executable(memory_tests tests/MemoryTests.cpp)
    target_link_libraries(memory_tests PRIVATE r2boy_core)
    add_test(NAME memory_tests COMMAND memory_tests)
    add_executable(cpu_tests tests/CPUTests.cpp)
    target_link_libraries(cpu_tests PRIVATE r2boy_core)
    add_test(NAME cpu_tests COMMAND cpu_tests)
    add_executable(serial_tests tests/SerialTests.cpp)
    target_link_libraries(serial_tests PRIVATE r2boy_core)
    add_test(NAME serial_tests COMMAND serial_tests)
    add_executable(timing_tests tests/TimingTests.cpp)
    target_link_libraries(timing_tests PRIVATE r2boy_core)
    add_test(NAME timing_tests COMMAND timing_tests)
    add_executable(cgb_speed_tests tests/CGBSpeedTests.cpp)
    target_link_libraries(cgb_speed_tests PRIVATE r2boy_core)
    add_test(NAME cgb_speed_tests COMMAND cgb_speed_tests)
    add_executable(timer_tests tests/TimerTests.cpp)
    target_link_libraries(timer_tests PRIVATE r2boy_core)
    add_test(NAME timer_tests COMMAND timer_tests)
    add_executable(dma_tests tests/DMATests.cpp)
    target_link_libraries(dma_tests PRIVATE r2boy_core)
    add_test(NAME dma_tests COMMAND dma_tests)
    add_executable(vram_dma_tests tests/VRAMDMATests.cpp)
    target_link_libraries(vram_dma_tests PRIVATE r2boy_core)
    add_test(NAME vram_dma_tests COMMAND vram_dma_tests)
    add_executable(ppu_tests tests/PPUTests.cpp)
    target_link_libraries(ppu_tests PRIVATE r2boy_core)
    add_test(NAME ppu_tests COMMAND ppu_tests)
    add_executable(input_tests tests/InputTests.cpp src/System/Input.cpp)
    target_link_libraries(input_tests PRIVATE r2boy_core SDL3::SDL3-static)
    add_test(NAME input_tests COMMAND input_tests)
    add_executable(window_input_tests tests/WindowInputTests.cpp src/Window/Window.cpp src/Window/FileManager.cpp src/Window/ConfigManager.cpp)
    target_link_libraries(window_input_tests PRIVATE r2boy_common r2boy_imgui)
    add_test(NAME window_input_tests COMMAND window_input_tests)
    add_test(NAME rewind_menu_tests COMMAND window_input_tests --rewind)
    set_tests_properties(window_input_tests rewind_menu_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
    add_executable(rom_runner tests/RomRunner.cpp)
    target_link_libraries(rom_runner PRIVATE r2boy_core)
    add_executable(timing_probe tests/TimingProbe.cpp)
    target_link_libraries(timing_probe PRIVATE r2boy_core)
    add_executable(window_startup_probe tests/WindowStartupProbe.cpp)
    target_link_libraries(window_startup_probe PRIVATE r2boy_core)
    add_executable(video_phase_tests tests/VideoPhaseTests.cpp)
    target_link_libraries(video_phase_tests PRIVATE r2boy_core)
    add_test(NAME video_phase_tests COMMAND video_phase_tests)
endif()
