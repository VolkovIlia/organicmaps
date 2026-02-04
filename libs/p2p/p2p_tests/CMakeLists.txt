project(p2p_tests)

set(SRC
  aggregated_traffic_share_test.cpp
  gossip_learning_test.cpp
  h3_aggregator_test.cpp
  privacy_manager_test.cpp
  privacy_settings_test.cpp
  rolling_id_generator_test.cpp
)

omim_add_test(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME} PRIVATE p2p)
