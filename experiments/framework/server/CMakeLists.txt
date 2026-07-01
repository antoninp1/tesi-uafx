add_library(uafx_common STATIC
    cli.c
    rt_functions.c
    sks_helpers.c
    establish_connection.c
    uafx_common.c
)
target_link_libraries(uafx_common PUBLIC uafx_types open62541::open62541)
target_include_directories(uafx_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(pub_test pub_test.c)
target_link_libraries(pub_test PRIVATE uafx_common)
target_compile_definitions(pub_test PRIVATE IS_PUBLISHER)

add_executable(sub_test sub_test.c)
target_link_libraries(sub_test PRIVATE uafx_common)
target_compile_definitions(sub_test PRIVATE IS_SUBSCRIBER)

add_executable(lds_server lds_server.c)
target_link_libraries(lds_server PRIVATE open62541::open62541)

add_executable(sks_server sks_server.c)
target_link_libraries(sks_server PRIVATE open62541::open62541)
