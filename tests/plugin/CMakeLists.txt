# Test plugin library
add_library(test_plugin SHARED
        ExamplePlugin.cpp
        ExamplePlugin.h
        ${CMAKE_SOURCE_DIR}/include/interface.h
)

target_include_directories(test_plugin PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Set plugin properties
set_target_properties(test_plugin PROPERTIES
        PREFIX ""
        SUFFIX ".so"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)