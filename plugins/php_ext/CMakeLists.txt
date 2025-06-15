# PHP plugin library
add_library(php_plugin SHARED
        ${CMAKE_SOURCE_DIR}/include/interface.h
        ${CMAKE_SOURCE_DIR}/plugins/php_ext/php_ext.cpp
        ${CMAKE_SOURCE_DIR}/plugins/php_ext/php_ext.h
)

target_include_directories(php_plugin PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/plugins/php_ext/php-src/
        ${CMAKE_SOURCE_DIR}/plugins/php_ext/php-src/main
        ${CMAKE_SOURCE_DIR}/plugins/php_ext/php-src/sapi
        ${CMAKE_SOURCE_DIR}/plugins/php_ext/php-src/TSRM
        ${CMAKE_SOURCE_DIR}/plugins/php_ext/php-src/Zend
        ${CMAKE_SOURCE_DIR}/plugins/php_ext/php-src/ext
)

target_link_libraries(php_plugin PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/php_ext/php-src/libs/libphp.so
)

# Set plugin properties
set_target_properties(php_plugin PROPERTIES
        PREFIX ""
        SUFFIX ".so"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)