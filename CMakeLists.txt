cmake_minimum_required(VERSION 3.21)
project(DSHDesktopQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebEngineWidgets)

# Release builds are WIN32 GUI apps (no console window). Debug keeps a
# console so qDebug() output is visible during development.
set(DSH_GUI_FLAG "")
if(WIN32 AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DSH_GUI_FLAG WIN32)
endif()

add_executable(DSHDesktop ${DSH_GUI_FLAG}
    src/main.cpp
    src/MainWindow.cpp src/MainWindow.h
    src/HarnessProcess.cpp src/HarnessProcess.h
    src/PluginInterface.h
    src/PluginManager.cpp src/PluginManager.h
)

target_include_directories(DSHDesktop PRIVATE src)
target_link_libraries(DSHDesktop PRIVATE Qt6::Widgets Qt6::Network Qt6::WebEngineWidgets)

# --- Demo plugin: build it and stage into <exe>/plugins/ ---
add_subdirectory(plugins/demo_plugin)
add_custom_command(TARGET DSHDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:DSHDesktop>/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:demo_plugin>
            $<TARGET_FILE_DIR:DSHDesktop>/plugins/
    COMMENT "Staging demo_plugin into <exe>/plugins/"
)
