cmake_minimum_required(VERSION 3.15)
project(TinyEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Targets ---------------------------------------------------------------
#
# Three of them, and the split is the point:
#
#   engine        a static library. Knows nothing about any particular game.
#   asteroids     the game. Links engine.
#   engine_tests  asserts about the engine's pure logic. Links engine.
#
# Everything used to compile into one executable, which made "game code
# depends on engine code, never the reverse" a convention that had to be
# enforced by reading. Now the compiler enforces it: the engine target simply
# cannot see src/game, so an #include pointing the wrong way fails to build.
#
# Adding a second game is now three lines — another add_executable that links
# engine — rather than a restructure.
file(GLOB ENGINE_SOURCES "${CMAKE_SOURCE_DIR}/src/engine/*.cpp")
file(GLOB GAME_SOURCES   "${CMAKE_SOURCE_DIR}/src/game/*.cpp")

add_library(engine STATIC ${ENGINE_SOURCES})

# PUBLIC, not PRIVATE: anything linking engine also needs these headers, so
# the games and the tests inherit the include path automatically.
target_include_directories(engine PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_executable(asteroids ${GAME_SOURCES})
target_link_libraries(asteroids PRIVATE engine)

add_executable(engine_tests ${CMAKE_SOURCE_DIR}/tests/engine_tests.cpp)
target_link_libraries(engine_tests PRIVATE engine)

# `ctest` in the build directory runs it; so does running the binary directly,
# which prints what passed.
enable_testing()
add_test(NAME engine_tests COMMAND engine_tests)

foreach(target engine asteroids engine_tests)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()

# Copy assets/ next to the game executable after every build.
#
# TextureCache resolves paths relative to the executable rather than the
# working directory, so "assets/x.png" means the same thing whether the game
# is launched from a shell, an IDE, or by double-clicking it. That only holds
# if the files are actually there, hence this step. (A multi-config generator
# like Visual Studio puts the binary in Release/ or Debug/, which is exactly
# why the destination is a generator expression and not a fixed path.)
add_custom_command(TARGET asteroids POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:asteroids>/assets
    COMMENT "Copying assets next to the executable"
)

# --- Find SDL2 -------------------------------------------------------------
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
# Either way the libraries are attached to `engine` as PUBLIC, so the game and
# the tests pick them up by linking engine and nothing else.
#
# See README.md for exact install instructions per platform.
find_package(SDL2 CONFIG QUIET)

if (SDL2_FOUND)
    find_package(SDL2_image CONFIG REQUIRED)

    target_link_libraries(engine PUBLIC
        SDL2::SDL2
        SDL2_image::SDL2_image
    )

    # SDL2's "main" shim (SDL2main) redefines main() so SDL can do its own
    # startup on Windows before handing control to your code. It belongs on
    # the GAME only: the test binary has an ordinary main() and must not have
    # it rewritten out from under it.
    if (TARGET SDL2::SDL2main)
        target_link_libraries(asteroids PRIVATE SDL2::SDL2main)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)

    target_include_directories(engine PUBLIC
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_IMAGE_INCLUDE_DIRS}
    )
    target_link_libraries(engine PUBLIC
        ${SDL2_LIBRARIES}
        ${SDL2_IMAGE_LIBRARIES}
    )
endif()
