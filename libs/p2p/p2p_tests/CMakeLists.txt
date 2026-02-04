project(p2p_tests)

set(SRC
  privacy_manager_test.cpp
  privacy_settings_test.cpp
  rolling_id_generator_test.cpp
)

omim_add_test(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME} PRIVATE p2p)
