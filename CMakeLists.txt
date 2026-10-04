cmake_minimum_required(VERSION 3.16)
project(EventHorizon LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# raylib собирается из исходников и линкуется статически.
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib)

set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})
set(FONT_REGULAR ${CMAKE_SOURCE_DIR}/assets/fonts/DejaVuSans.ttf)
set(FONT_BOLD ${CMAKE_SOURCE_DIR}/assets/fonts/DejaVuSans-Bold.ttf)
add_custom_command(
    OUTPUT ${GEN_DIR}/EmbeddedFonts.cpp
    COMMAND ${CMAKE_COMMAND} -DOUT_DIR=${GEN_DIR} -DREGULAR=${FONT_REGULAR} -DBOLD=${FONT_BOLD}
            -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${FONT_REGULAR} ${FONT_BOLD} ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "Встраиваю шрифты")

add_executable(event_horizon
    src/main.cpp src/Game.cpp src/Audio.cpp ${GEN_DIR}/EmbeddedFonts.cpp)
target_include_directories(event_horizon PRIVATE src)
target_link_libraries(event_horizon PRIVATE raylib)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(event_horizon PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    if(NOT APPLE)
        # Свой libstdc++ внутри бинарника — меньше зависимостей у игрока.
        target_link_options(event_horizon PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()

add_executable(balance_sim EXCLUDE_FROM_ALL tools/sim.cpp src/Game.cpp)
target_include_directories(balance_sim PRIVATE src)
