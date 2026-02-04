project(traffic_tests)

set(SRC
  historical_speed_test.cpp
  historical_traffic_provider_test.cpp
  osm_speed_inference_test.cpp
  personal_speed_storage_test.cpp
  traffic_estimator_test.cpp
  traffic_info_test.cpp
  traffic_ml_model_test.cpp
)

omim_add_test(${PROJECT_NAME} ${SRC} REQUIRE_QT)

target_link_libraries(${PROJECT_NAME} PRIVATE traffic)
