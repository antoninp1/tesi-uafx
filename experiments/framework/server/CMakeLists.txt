add_executable(pub_test pub_test.c establish_connection.c rt_functions.c cli.c)
target_link_libraries(pub_test PRIVATE uafx_types)
target_compile_definitions(pub_test PRIVATE IS_PUBLISHER)

add_executable(sub_test sub_test.c establish_connection.c rt_functions.c cli.c)
target_link_libraries(sub_test PRIVATE uafx_types)
target_compile_definitions(sub_test PRIVATE IS_SUBSCRIBER)

add_executable(sks_pub_test sks/pub_test.c establish_connection.c rt_functions.c cli.c)
target_link_libraries(sks_pub_test PRIVATE uafx_types)
target_compile_definitions(sks_pub_test PRIVATE IS_PUBLISHER)

add_executable(sks_sub_test sks/sub_test.c establish_connection.c rt_functions.c cli.c)
target_link_libraries(sks_sub_test PRIVATE uafx_types)
target_compile_definitions(sks_sub_test PRIVATE IS_SUBSCRIBER)


add_executable(lds_server lds_server.c)
target_link_libraries(lds_server PRIVATE open62541::open62541)

add_executable(sks_server sks/sks_server.c)
target_link_libraries(sks_server PRIVATE open62541::open62541)
