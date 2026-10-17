cmake_minimum_required(VERSION 3.14)
project(R2BOY VERSION 0.0.1 LANGUAGES C CXX)

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

FetchContent_Declare(miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.1.0
    GIT_SHALLOW TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(miniz)

# Usamos a API mz_*; dispensa os aliases zlib que geram avisos de funções não utilizadas.
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

# Descobre os arquivos do projeto e verifica inclusões/remoções a cada build.
file(GLOB_RECURSE R2BOY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)

# O cartucho é compartilhado com os testes e deve ser compilado apenas na biblioteca.
file(GLOB_RECURSE R2BOY_CARTRIDGE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/Cartridge/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/Cartridge/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/Cartridge/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/Cartridge/*.hpp"
)
list(APPEND R2BOY_CARTRIDGE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/System/RomFile.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/System/RomFile.h"
)
list(REMOVE_ITEM R2BOY_SOURCES ${R2BOY_CARTRIDGE_SOURCES})

add_library(r2boy_cartridge STATIC ${R2BOY_CARTRIDGE_SOURCES})
target_compile_features(r2boy_cartridge PUBLIC cxx_std_17)
target_include_directories(r2boy_cartridge PUBLIC src)
target_link_libraries(r2boy_cartridge PRIVATE miniz)

add_executable(R2BOY ${R2BOY_SOURCES})
target_compile_features(R2BOY PRIVATE cxx_std_17)
set_target_properties(R2BOY PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(R2BOY PRIVATE src)
target_link_libraries(R2BOY PRIVATE SDL2::SDL2main r2boy_imgui r2boy_cartridge)
if(MSVC)
    target_compile_options(R2BOY PRIVATE /W4)
    target_compile_options(r2boy_cartridge PRIVATE /W4)
else()
    target_compile_options(R2BOY PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(r2boy_cartridge PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(cartridge_tests tests/CartridgeTests.cpp)
    target_link_libraries(cartridge_tests PRIVATE r2boy_cartridge miniz)
    add_test(NAME cartridge_tests COMMAND cartridge_tests)
endif()
