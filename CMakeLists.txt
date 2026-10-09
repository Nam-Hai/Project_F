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
  core/sample_player.cpp
  core/sequencer.cpp
  core/synth_kick.cpp
)
target_include_directories(core PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(core PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)

# Plateforme PC : fichiers (WAV) et sortie audio.
add_library(desktop_io STATIC
  platform/desktop/sample_loader.cpp
  platform/desktop/wav_writer.cpp
)
target_include_directories(desktop_io PUBLIC ${CMAKE_SOURCE_DIR})

add_library(desktop_audio STATIC platform/desktop/audio_miniaudio.cpp)
target_include_directories(desktop_audio PUBLIC ${CMAKE_SOURCE_DIR})
if(APPLE)
  target_link_libraries(desktop_audio PUBLIC
    "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
  target_link_libraries(desktop_audio PUBLIC pthread dl m)
endif()

add_executable(drum_machine platform/desktop/main.cpp)
target_link_libraries(drum_machine PRIVATE core desktop_io desktop_audio)
target_compile_definitions(drum_machine PRIVATE SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples")

add_executable(tone_test platform/desktop/tone_test.cpp)
target_link_libraries(tone_test PRIVATE core desktop_audio)

add_executable(render_wav platform/desktop/render_wav.cpp)
target_link_libraries(render_wav PRIVATE core desktop_io)

add_executable(sample_demo platform/desktop/sample_demo.cpp)
target_link_libraries(sample_demo PRIVATE core desktop_io)

# Tests
enable_testing()
add_executable(test_kick tests/test_kick.cpp)
target_link_libraries(test_kick PRIVATE core)
add_test(NAME kick COMMAND test_kick)

add_executable(test_sample tests/test_sample.cpp)
target_link_libraries(test_sample PRIVATE core desktop_io)
add_test(NAME sample COMMAND test_sample)

add_executable(test_sequencer tests/test_sequencer.cpp)
target_link_libraries(test_sequencer PRIVATE core)
add_test(NAME sequencer COMMAND test_sequencer)
