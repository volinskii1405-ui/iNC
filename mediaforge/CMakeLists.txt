cmake_minimum_required(VERSION 3.16)
project(MediaForge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Concurrent Multimedia Test)

# FFmpeg is built by scripts/build_ffmpeg.sh as static libraries with the codecs
# baked in, so the app carries its own decoders and depends only on glibc for them.
set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/third_party/ffmpeg" CACHE PATH "FFmpeg install prefix")
set(FFMPEG_STATIC_DEPS "${CMAKE_SOURCE_DIR}/third_party/static-libs" CACHE PATH "Static codec libraries")
if(NOT EXISTS "${FFMPEG_ROOT}/lib/libavcodec.a")
    message(FATAL_ERROR "FFmpeg not found in ${FFMPEG_ROOT}. Run scripts/build_ffmpeg.sh first (build.sh does it).")
endif()
add_library(ffmpeg_static INTERFACE)
target_include_directories(ffmpeg_static SYSTEM INTERFACE "${FFMPEG_ROOT}/include")
set(_ff "${FFMPEG_ROOT}/lib")
set(_dep "${FFMPEG_STATIC_DEPS}")
target_link_libraries(ffmpeg_static INTERFACE
    -Wl,--start-group
    ${_ff}/libavfilter.a ${_ff}/libavformat.a ${_ff}/libavcodec.a
    ${_ff}/libswscale.a ${_ff}/libswresample.a ${_ff}/libavutil.a
    ${_dep}/libx264.a ${_dep}/libvpx.a ${_dep}/libmp3lame.a
    ${_dep}/libvorbisenc.a ${_dep}/libvorbis.a ${_dep}/libogg.a ${_dep}/libopus.a
    -Wl,--end-group
    z m pthread dl atomic)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    src/common/*.cpp src/common/*.h
    src/image/*.cpp src/image/*.h
    src/media/*.cpp src/media/*.h)
add_library(mfcore STATIC ${CORE_SOURCES})
target_include_directories(mfcore PUBLIC src)
target_link_libraries(mfcore PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent ffmpeg_static)

file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS
    src/app/*.cpp src/app/*.h
    src/imageui/*.cpp src/imageui/*.h
    src/videoui/*.cpp src/videoui/*.h
    src/audioui/*.cpp src/audioui/*.h)
qt_add_resources(UI_RESOURCES assets/resources.qrc)
add_library(mfui STATIC ${UI_SOURCES})
target_link_libraries(mfui PUBLIC mfcore Qt6::Widgets Qt6::Multimedia)
add_executable(mediaforge src/main.cpp ${UI_RESOURCES})
target_link_libraries(mediaforge PRIVATE mfui)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mfcore PRIVATE -Wall -Wextra)
    target_compile_options(mfui PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    target_compile_options(mediaforge PRIVATE -Wall -Wextra)
endif()
target_compile_definitions(mfcore PUBLIC MF_VERSION="${PROJECT_VERSION}")

# The export pipeline runs the ffmpeg binary that sits next to the executable.
add_custom_command(TARGET mediaforge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/ffmpeg" "$<TARGET_FILE_DIR:mediaforge>/ffmpeg"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/ffprobe" "$<TARGET_FILE_DIR:mediaforge>/ffprobe")

enable_testing()
add_executable(mf_tests tests/test_main.cpp tests/test_image.cpp tests/test_media.cpp tests/TestUtil.h)
target_link_libraries(mf_tests PRIVATE mfcore Qt6::Test Qt6::Gui)
target_compile_definitions(mf_tests PRIVATE MF_TEST_FFMPEG="${FFMPEG_ROOT}/bin/ffmpeg")
add_test(NAME core COMMAND mf_tests)
set_tests_properties(core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(mf_gui_tests tests/test_gui.cpp ${UI_RESOURCES})
target_link_libraries(mf_gui_tests PRIVATE mfui Qt6::Test)
target_compile_definitions(mf_gui_tests PRIVATE MF_TEST_FFMPEG="${FFMPEG_ROOT}/bin/ffmpeg")
add_test(NAME gui COMMAND mf_gui_tests)
set_tests_properties(gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

install(TARGETS mediaforge RUNTIME DESTINATION bin)
