cmake_minimum_required(VERSION 3.15)
project(TinyEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Collect engine source files ---
file(GLOB ENGINE_SOURCES "${CMAKE_SOURCE_DIR}/src/engine/*.cpp")
file(GLOB GAME_SOURCES   "${CMAKE_SOURCE_DIR}/src/game/*.cpp")

add_executable(tiny_engine
    ${ENGINE_SOURCES}
    ${GAME_SOURCES}
)

target_include_directories(tiny_engine PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Copy assets/ next to the executable after every build.
#
# TextureCache resolves paths relative to the executable rather than the
# working directory, so "assets/snake.png" means the same thing whether the
# game is launched from a shell, an IDE, or by double-clicking it. That only
# holds if the files are actually there, hence this step. (A multi-config
# generator like Visual Studio puts the binary in Release/ or Debug/, which
# is exactly why the destination is a generator expression and not a fixed
# path.)
add_custom_command(TARGET tiny_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:tiny_engine>/assets
    COMMENT "Copying assets next to the executable"
)

if (MSVC)
    target_compile_options(tiny_engine PRIVATE /W4)
else()
    target_compile_options(tiny_engine PRIVATE -Wall -Wextra)
endif()

# --- Find SDL2 ---
# SDL2 shows up two different ways depending on platform and how it was
# installed, so we try both:
#
#   1. As a CMake "config" package (SDL2Config.cmake / SDL2Targets.cmake).
#      This is how vcpkg installs it on Windows, and gives clean
#      SDL2::SDL2 / SDL2::SDL2main targets with no manual include/lib paths.
#   2. As a pkg-config .pc file. This is how apt (Linux) and Homebrew
#      (macOS) install it. There's no vcpkg-style target, so we wire up the
#      include dirs and libraries pkg-config reports by hand.
#
# See README.md for exact install instructions per platform.
find_package(SDL2 CONFIG QUIET)

if (SDL2_FOUND)
    find_package(SDL2_image CONFIG REQUIRED)

    # SDL2's "main" shim (SDL2main) redefines main() so SDL can do its own
    # startup on Windows before handing control to your code. It only
    # exists as a target when SDL2 was installed with it (e.g. via vcpkg).
    if (TARGET SDL2::SDL2main)
        target_link_libraries(tiny_engine PRIVATE SDL2::SDL2main)
    endif()

    target_link_libraries(tiny_engine PRIVATE
        SDL2::SDL2
        SDL2_image::SDL2_image
    )
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)

    target_include_directories(tiny_engine PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_IMAGE_INCLUDE_DIRS}
    )
    target_link_libraries(tiny_engine PRIVATE
        ${SDL2_LIBRARIES}
        ${SDL2_IMAGE_LIBRARIES}
    )
endif()
