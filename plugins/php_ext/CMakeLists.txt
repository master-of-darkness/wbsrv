# Check if libphp.so exists
set(LIBPHP_PATH "${CMAKE_SOURCE_DIR}/plugins/php_ext/php-src/libs/libphp.so")
set(PHP_BUILD_SCRIPT "${CMAKE_SOURCE_DIR}/plugins/php_ext/build_php.sh")

if(NOT EXISTS ${LIBPHP_PATH})
    message(STATUS "libphp.so not found. Building PHP from source...")

    # Execute the PHP build script
    execute_process(
            COMMAND bash ${PHP_BUILD_SCRIPT}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/plugins/php_ext
            RESULT_VARIABLE PHP_BUILD_RESULT
            OUTPUT_VARIABLE PHP_BUILD_OUTPUT
            ERROR_VARIABLE PHP_BUILD_ERROR
    )

    # Check if the build was successful
    if(NOT PHP_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "PHP build failed with error: ${PHP_BUILD_ERROR}")
    endif()

    # Verify that libphp.so was created
    if(NOT EXISTS ${LIBPHP_PATH})
        message(FATAL_ERROR "PHP build completed but libphp.so was not found at expected location: ${LIBPHP_PATH}")
    endif()

    message(STATUS "PHP build completed successfully")
else()
    message(STATUS "libphp.so found. Skipping PHP build.")
endif()

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
        ${LIBPHP_PATH}
)

# Set plugin properties
set_target_properties(php_plugin PROPERTIES
        PREFIX ""
        SUFFIX ".so"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

# Add dependency to ensure PHP is built before the plugin
add_dependencies(php_plugin php_build_check)

# Create a custom target for the PHP build check
add_custom_target(php_build_check
        COMMAND ${CMAKE_COMMAND} -E echo "PHP build check completed"
)