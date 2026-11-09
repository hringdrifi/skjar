cmake_minimum_required(VERSION 3.16)
project(skjar_ui C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 描画先：fbdev（実機）、sdl（Linux / WSL の窓）、windows（Windows の窓）。lv_conf.h も参照
if(WIN32)
    set(SKJAR_UI_BACKEND "windows" CACHE STRING "Display backend: fbdev, sdl or windows")
else()
    set(SKJAR_UI_BACKEND "fbdev" CACHE STRING "Display backend: fbdev, sdl or windows")
endif()
set_property(CACHE SKJAR_UI_BACKEND PROPERTY STRINGS fbdev sdl windows)

# LVGL の CMake は、依存ライブラリ（SDL2、libevdev）を CONFIG_LV_USE_* で探す。lv_conf.h とそろえる
set(CONFIG_LV_USE_SDL OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_EVDEV OFF CACHE BOOL "" FORCE)
if(SKJAR_UI_BACKEND STREQUAL "sdl")
    add_compile_definitions(SKJAR_UI_SDL=1)
    set(CONFIG_LV_USE_SDL ON CACHE BOOL "" FORCE)
elseif(SKJAR_UI_BACKEND STREQUAL "windows")
    add_compile_definitions(SKJAR_UI_WINDOWS=1)
elseif(SKJAR_UI_BACKEND STREQUAL "fbdev")
    add_compile_definitions(SKJAR_UI_FBDEV=1)
    set(CONFIG_LV_USE_EVDEV ON CACHE BOOL "" FORCE)
else()
    message(FATAL_ERROR "SKJAR_UI_BACKEND must be fbdev, sdl or windows")
endif()

set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)

include(FetchContent)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.6.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(lvgl)

add_executable(skjar-ui
    src/main.c
    src/ui.c
    src/link.c
    src/serial_port.c
    src/platform.c
    src/trackpad.c
    src/touch_source.c
    src/keylabel.c
    src/icons.c
    src/background.c
    src/settings.c
    src/sysinfo.c
    src/backlight.c)
if(NOT MSVC)
    target_compile_options(skjar-ui PRIVATE -Wall -Wextra)
endif()
target_link_libraries(skjar-ui PRIVATE lvgl::lvgl)
if(NOT WIN32)
    target_link_libraries(skjar-ui PRIVATE m)
endif()
if(SKJAR_UI_BACKEND STREQUAL "windows")
    # コンソールのアプリのまま（ログが見える）、Win32 の窓を開く
    target_link_libraries(skjar-ui PRIVATE user32 gdi32 winmm)
endif()

install(TARGETS skjar-ui RUNTIME DESTINATION bin)

# UART の行の組み立てと読み取りのテスト（ctest で走らせる）
enable_testing()
add_executable(link-test tests/link_test.c src/link.c src/serial_port.c src/platform.c)
if(NOT MSVC)
    target_compile_options(link-test PRIVATE -Wall -Wextra)
endif()
add_test(NAME link COMMAND link-test)

add_executable(trackpad-test tests/trackpad_test.c src/trackpad.c)
if(NOT MSVC)
    target_compile_options(trackpad-test PRIVATE -Wall -Wextra)
endif()
if(NOT WIN32)
    target_link_libraries(trackpad-test PRIVATE m)
endif()
add_test(NAME trackpad COMMAND trackpad-test)

add_executable(keylabel-test tests/keylabel_test.c src/keylabel.c)
target_link_libraries(keylabel-test PRIVATE lvgl::lvgl)
add_test(NAME keylabel COMMAND keylabel-test)
