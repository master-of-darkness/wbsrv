# Main wbsrv executable
file(GLOB_RECURSE WBSRV_SRC "*.cpp")

add_executable(wbsrv ${WBSRV_SRC} "../external/xxhash.c")

# Check if libphp.so exists
set(LIBPHP_PATH "${CMAKE_SOURCE_DIR}/external/php-src/libs/libphp.so")
set(PHP_BUILD_SCRIPT "${CMAKE_SOURCE_DIR}/external/build_php.sh")

if (NOT EXISTS ${LIBPHP_PATH})
    message(STATUS "libphp.so not found. Building PHP from source...")

    # Execute the PHP build script
    execute_process(
            COMMAND bash ${PHP_BUILD_SCRIPT}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/external/
            RESULT_VARIABLE PHP_BUILD_RESULT
            OUTPUT_VARIABLE PHP_BUILD_OUTPUT
            ERROR_VARIABLE PHP_BUILD_ERROR
    )

    # Check if the build was successful
    if (NOT PHP_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "PHP build failed with error: ${PHP_BUILD_ERROR}")
    endif ()

    # Verify that libphp.so was created
    if (NOT EXISTS ${LIBPHP_PATH})
        message(FATAL_ERROR "PHP build completed but libphp.so was not found at expected location: ${LIBPHP_PATH}")
    endif ()

    message(STATUS "PHP build completed successfully")
else ()
    message(STATUS "libphp.so found. Skipping PHP build.")
endif ()

target_include_directories(wbsrv PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/external/php-src/
        ${CMAKE_SOURCE_DIR}/external/php-src/main
        ${CMAKE_SOURCE_DIR}/external/php-src/sapi
        ${CMAKE_SOURCE_DIR}/external/php-src/TSRM
        ${CMAKE_SOURCE_DIR}/external/php-src/Zend
        ${CMAKE_SOURCE_DIR}/external/php-src/ext
        ${CMAKE_SOURCE_DIR}
        ${PROCESSED_INCLUDE_DIRS}
)

target_link_libraries(wbsrv PRIVATE
        ${LIBPHP_PATH}
        proxygen::proxygen
        proxygen::proxygenhttpserver
        yaml-cpp::yaml-cpp
)

target_compile_definitions(wbsrv PRIVATE $<$<CONFIG:Debug>:DEBUG>)