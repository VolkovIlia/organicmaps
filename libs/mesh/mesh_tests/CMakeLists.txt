project(mesh_tests)

set(SRC
  ble_protocol_test.cpp
  connection_manager_test.cpp
)

omim_add_test(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME} PRIVATE mesh)
