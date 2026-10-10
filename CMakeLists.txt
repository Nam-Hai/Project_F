cmake_minimum_required(VERSION 3.16)
project(drum_machine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Core portable : aucune dépendance OS (sera compilé tel quel pour Teensy).
add_library(core STATIC
  core/engine.cpp
  core/mixer.cpp
  core/profile/profile_codec.cpp
  core/sample_player.cpp
  core/sequencer.cpp
  core/synth_kick.cpp
  core/ui/canvas.cpp
  core/ui/ui_controller.cpp
)
target_include_directories(core PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(core PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)

# Plateforme PC : fichiers (WAV) et sortie audio.
add_library(desktop_io STATIC
  platform/desktop/profile_store_file.cpp
  platform/desktop/sample_bank.cpp
  platform/desktop/sample_library.cpp
  platform/desktop/sample_loader.cpp
  platform/desktop/wav_writer.cpp
)
target_include_directories(desktop_io PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(desktop_io PUBLIC core)

add_library(desktop_loop STATIC platform/desktop/default_loop.cpp)
target_link_libraries(desktop_loop PUBLIC desktop_io)
target_compile_definitions(desktop_loop PUBLIC SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples" PROFILES_DIR="${CMAKE_SOURCE_DIR}/profiles")

add_library(desktop_audio STATIC platform/desktop/audio_miniaudio.cpp)
target_include_directories(desktop_audio PUBLIC ${CMAKE_SOURCE_DIR})
if(APPLE)
  target_link_libraries(desktop_audio PUBLIC
    "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
  target_link_libraries(desktop_audio PUBLIC pthread dl m)
endif()

add_executable(drum_machine platform/desktop/main.cpp)
target_link_libraries(drum_machine PRIVATE desktop_loop desktop_audio)

add_executable(canvas_preview platform/desktop/canvas_preview.cpp)
target_link_libraries(canvas_preview PRIVATE core)

# Émulateur PC (Dear ImGui + SDL2) : optionnel, jamais embarqué sur Teensy.
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_impl_sdl2.cpp
    third_party/imgui/imgui_impl_sdlrenderer2.cpp
  )
  target_include_directories(imgui PUBLIC third_party/imgui)
  target_link_libraries(imgui PUBLIC SDL2::SDL2)

  add_executable(canvas_window platform/desktop/canvas_window.cpp)
  target_link_libraries(canvas_window PRIVATE core imgui)

  add_executable(emulator platform/desktop/emulator.cpp)
  target_link_libraries(emulator PRIVATE desktop_loop desktop_audio imgui)
else()
  message(STATUS "SDL2 introuvable : l'émulateur (canvas_window) n'est pas compilé")
endif()

add_executable(tone_test platform/desktop/tone_test.cpp)
target_link_libraries(tone_test PRIVATE core desktop_audio)

add_executable(render_wav platform/desktop/render_wav.cpp)
target_link_libraries(render_wav PRIVATE desktop_io)

add_executable(sample_demo platform/desktop/sample_demo.cpp)
target_link_libraries(sample_demo PRIVATE desktop_io)

# Tests
enable_testing()
add_executable(test_kick tests/test_kick.cpp)
target_link_libraries(test_kick PRIVATE core)
add_test(NAME kick COMMAND test_kick)

add_executable(test_sample tests/test_sample.cpp)
target_link_libraries(test_sample PRIVATE desktop_io)
add_test(NAME sample COMMAND test_sample)

add_executable(test_sequencer tests/test_sequencer.cpp)
target_link_libraries(test_sequencer PRIVATE core)
add_test(NAME sequencer COMMAND test_sequencer)

add_executable(test_live tests/test_live.cpp)
target_link_libraries(test_live PRIVATE core)
add_test(NAME live COMMAND test_live)

add_executable(test_canvas tests/test_canvas.cpp)
target_link_libraries(test_canvas PRIVATE core)
add_test(NAME canvas COMMAND test_canvas)

add_executable(test_ui tests/test_ui.cpp)
target_link_libraries(test_ui PRIVATE core)
add_test(NAME ui COMMAND test_ui)

add_executable(test_profile tests/test_profile.cpp)
target_link_libraries(test_profile PRIVATE core)
add_test(NAME profile COMMAND test_profile)

add_executable(test_profile_store tests/test_profile_store.cpp)
target_link_libraries(test_profile_store PRIVATE desktop_io)
add_test(NAME profile_store COMMAND test_profile_store)

add_executable(test_loops tests/test_loops.cpp)
target_link_libraries(test_loops PRIVATE core)
add_test(NAME loops COMMAND test_loops)

add_executable(test_integration tests/test_integration.cpp)
target_link_libraries(test_integration PRIVATE desktop_io)
add_test(NAME integration COMMAND test_integration)

add_executable(test_step_params tests/test_step_params.cpp)
target_link_libraries(test_step_params PRIVATE core)
add_test(NAME step_params COMMAND test_step_params)

add_executable(test_step_edit tests/test_step_edit.cpp)
target_link_libraries(test_step_edit PRIVATE core)
add_test(NAME step_edit COMMAND test_step_edit)

add_executable(test_note tests/test_note.cpp)
target_link_libraries(test_note PRIVATE core)
add_test(NAME note COMMAND test_note)

add_executable(test_keyboard tests/test_keyboard.cpp)
target_link_libraries(test_keyboard PRIVATE core)
add_test(NAME keyboard COMMAND test_keyboard)
