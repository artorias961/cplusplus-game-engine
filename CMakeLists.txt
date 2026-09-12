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

add_library(engine STATIC ${ENGINE_SOURCES})

# PUBLIC, not PRIVATE: anything linking engine also needs these headers, so
# the games and the tests inherit the include path automatically.
target_include_directories(engine PUBLIC ${CMAKE_SOURCE_DIR}/include)

# --- The games --------------------------------------------------------------
#
# Both are shaped the same way: the rules in a library, and a three-line
# main.cpp that opens a window. That split exists for one reason — a game whose
# logic lives inside main.cpp cannot be tested, because reaching any of it
# means opening a window. With the rules in a library, the test binaries link
# the same code the player runs and drive it headlessly.
add_library(asteroids_lib STATIC
    "${CMAKE_SOURCE_DIR}/src/game/asteroids/Asteroids.cpp")
target_include_directories(asteroids_lib PUBLIC
    "${CMAKE_SOURCE_DIR}/src/game/asteroids")
target_link_libraries(asteroids_lib PUBLIC engine)

add_executable(asteroids "${CMAKE_SOURCE_DIR}/src/game/asteroids/main.cpp")
target_link_libraries(asteroids PRIVATE asteroids_lib)

add_library(breakout_lib STATIC
    "${CMAKE_SOURCE_DIR}/src/game/breakout/Breakout.cpp")
target_include_directories(breakout_lib PUBLIC
    "${CMAKE_SOURCE_DIR}/src/game/breakout")
target_link_libraries(breakout_lib PUBLIC engine)

add_executable(breakout "${CMAKE_SOURCE_DIR}/src/game/breakout/main.cpp")
target_link_libraries(breakout PRIVATE breakout_lib)

add_library(lanebattle_lib STATIC
    "${CMAKE_SOURCE_DIR}/src/game/lanebattle/LaneBattle.cpp"
    "${CMAKE_SOURCE_DIR}/src/game/lanebattle/Weather.cpp")
target_include_directories(lanebattle_lib PUBLIC
    "${CMAKE_SOURCE_DIR}/src/game/lanebattle")
target_link_libraries(lanebattle_lib PUBLIC engine)

add_executable(lanebattle "${CMAKE_SOURCE_DIR}/src/game/lanebattle/main.cpp")
target_link_libraries(lanebattle PRIVATE lanebattle_lib)

add_executable(weather_preview "${CMAKE_SOURCE_DIR}/tests/weather_preview.cpp")
target_link_libraries(weather_preview PRIVATE lanebattle_lib)
add_test(NAME weather_render_tests COMMAND weather_preview --verify)

# --- Tests ------------------------------------------------------------------
add_executable(engine_tests "${CMAKE_SOURCE_DIR}/tests/engine_tests.cpp")
target_link_libraries(engine_tests PRIVATE engine)

# The renderer, checked against real pixels rather than by eye. It opens a
# window through SDL's "dummy" video driver, which needs no display, so this
# runs anywhere the other tests do.
add_executable(render_tests "${CMAKE_SOURCE_DIR}/tests/render_tests.cpp")
target_link_libraries(render_tests PRIVATE engine)

add_executable(breakout_tests "${CMAKE_SOURCE_DIR}/tests/breakout_tests.cpp")
target_include_directories(breakout_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(breakout_tests PRIVATE breakout_lib)

# Not a test: it measures rather than asserts, so it is deliberately not
# registered with ctest. It exists so "optimise when it's slow" can be
# answered with a number instead of a guess.
add_executable(engine_bench "${CMAKE_SOURCE_DIR}/tests/engine_bench.cpp")
target_link_libraries(engine_bench PRIVATE engine)

add_executable(lanebattle_tests "${CMAKE_SOURCE_DIR}/tests/lanebattle_tests.cpp")
target_include_directories(lanebattle_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(lanebattle_tests PRIVATE lanebattle_lib)

add_executable(asteroids_tests "${CMAKE_SOURCE_DIR}/tests/asteroids_tests.cpp")
target_include_directories(asteroids_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(asteroids_tests PRIVATE asteroids_lib)

# Not a test either, for the same reason engine_bench is not: it prints a table
# to read rather than passing or failing, and it plays hundreds of whole
# battles, which takes about a minute.
#
# It is the instrument for tuning the stage table. Every previous attempt at
# that curve was wrong in a way that was invisible in the numbers and obvious
# after one run of something like this.
add_executable(campaign_probe "${CMAKE_SOURCE_DIR}/tests/campaign_probe.cpp")
target_include_directories(campaign_probe PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(campaign_probe PRIVATE lanebattle_lib)

# Also not a test, and this one CANNOT fail: it writes a PNG of every screen in
# the game and a person has to look at them. That is the point. The bug class
# it exists for — a panel over another panel, a label off the edge, a whole
# menu still drawn over the battlefield — is exactly the class no assertion
# catches, and one of those shipped for weeks.
add_executable(ui_shots "${CMAKE_SOURCE_DIR}/tests/ui_shots.cpp")
target_include_directories(ui_shots PRIVATE "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(ui_shots PRIVATE lanebattle_lib)

# `ctest` in the build directory runs all three; so does running any of the
# binaries directly, which prints what passed.
enable_testing()
add_test(NAME engine_tests COMMAND engine_tests)
add_test(NAME breakout_tests COMMAND breakout_tests)
add_test(NAME asteroids_tests COMMAND asteroids_tests)
add_test(NAME lanebattle_tests COMMAND lanebattle_tests)
add_test(NAME render_tests COMMAND render_tests)

# Belt and braces: render_tests sets this itself if it is unset, but saying it
# here means a developer running the binary by hand on a desktop gets the same
# headless behaviour CI does, rather than a window flashing up.
set_tests_properties(render_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

# --- Does the game actually start? ------------------------------------------
#
# Every game's RULES are tested by driving its scenes through tests/Harness.h,
# but the harness reimplements the loop rather than using it. That left one
# stretch of the project executed by nobody: main.cpp, the real Engine::run,
# window creation, texture loading, audio startup and shutdown. A game that
# crashed on its first frame, or failed to find its assets, would have passed
# every test in the suite.
#
# These run the SHIPPED EXECUTABLE for half a second and require it to exit 0.
# It is the shallowest possible test and it covers the deepest untested seam.
#
# Labelled `smoke` so CI can run them once rather than twenty times. The
# repeated run exists to shake out flaky SIMULATION — a race in the rules, an
# uninitialised value that only bites sometimes. Starting a process is not that
# kind of test, and running it twenty times doubled the pipeline for nothing.
#
#   ctest -LE smoke --repeat until-fail:20    the flake hunt
#   ctest -L  smoke                           the games start
foreach(game asteroids breakout lanebattle)
    add_test(NAME ${game}_starts COMMAND ${game})
    set_tests_properties(${game}_starts PROPERTIES
        LABELS "smoke"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;TINY_ENGINE_MAX_FRAMES=30")
endforeach()

foreach(target engine asteroids_lib asteroids breakout_lib breakout
                lanebattle_lib lanebattle engine_tests breakout_tests
                asteroids_tests lanebattle_tests render_tests engine_bench)
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
#
# The test binaries get them too. That is not tidiness: lanebattle_tests loads
# the shipped assets/lanebattle/units.txt and checks it parses into a sane
# roster, so the data file the player actually gets is covered rather than
# only the parser that reads it.
foreach(target asteroids lanebattle lanebattle_tests campaign_probe ui_shots weather_preview)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/assets
                $<TARGET_FILE_DIR:${target}>/assets
        COMMENT "Copying assets next to ${target}"
    )
endforeach()

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
    # the GAMES only: a test binary has an ordinary main() and must not have
    # it rewritten out from under it. (The test sources also define
    # SDL_MAIN_HANDLED, which stops SDL.h doing the same thing by macro.)
    if (TARGET SDL2::SDL2main)
        target_link_libraries(asteroids PRIVATE SDL2::SDL2main)
        target_link_libraries(breakout PRIVATE SDL2::SDL2main)
        target_link_libraries(lanebattle PRIVATE SDL2::SDL2main)
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
