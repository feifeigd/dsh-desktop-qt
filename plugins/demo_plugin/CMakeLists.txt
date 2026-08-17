# Demo plugin for DSH Desktop.
# Shows how a plugin contributes:
#   1. a native Qt panel (system info widget) in the left plugin bar
#   2. a tray menu action ("System Info…")
#   3. a slash command "/sysinfo" from the web chat
add_library(demo_plugin SHARED demo_plugin.cpp demo_plugin.h)
target_include_directories(demo_plugin PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(demo_plugin PRIVATE Qt6::Widgets)

# Qt plugin metadata: exported via the IID so QPluginLoader can match it.
set_target_properties(demo_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "demo_plugin"
)
