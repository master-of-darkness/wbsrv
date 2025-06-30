

add_executable(test_server
    test_server.cpp
)

target_link_libraries(test_server
PRIVATE
        proxygen::proxygen
        proxygen::proxygenhttpserver

)

add_subdirectory(plugin)

