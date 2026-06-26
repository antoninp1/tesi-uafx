add_executable(pub_test pub_test.c establish_connection.c)
target_link_libraries(pub_test PRIVATE uafx_types)

add_executable(sub_test sub_test.c establish_connection.c)
target_link_libraries(sub_test PRIVATE uafx_types)

add_executable(lds_server lds_server.c)
target_link_libraries(lds_server PRIVATE open62541::open62541)
