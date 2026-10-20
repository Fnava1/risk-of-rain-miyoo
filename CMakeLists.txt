cmake_minimum_required(VERSION 3.13)
project(RiskOfRainMiyoo C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    src
    src/platform
    src/renderer
    src/audio
    src/vm
)

file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/*.c"
)

add_executable(RiskOfRain ${SOURCES})

target_link_libraries(RiskOfRain
    SDL2
    pthread
    dl
    m
)
