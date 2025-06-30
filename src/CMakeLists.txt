# Main wbsrv executable
file(GLOB_RECURSE WBSRV_SRC "*.cpp")

add_executable(wbsrv ${WBSRV_SRC} ${CMAKE_SOURCE_DIR}/include/interface.h)

target_include_directories(wbsrv PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${PROCESSED_INCLUDE_DIRS}
)

target_link_libraries(wbsrv PRIVATE
        proxygen::proxygen
        proxygen::proxygenhttpserver
        yaml-cpp::yaml-cpp
        xxHash::xxhash
)

target_compile_definitions(wbsrv PRIVATE $<$<CONFIG:Debug>:DEBUG>)