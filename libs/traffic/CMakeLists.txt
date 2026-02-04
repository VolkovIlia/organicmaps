project(traffic)

set(SRC
  historical_speed_data.cpp
  historical_speed_data.hpp
  historical_speed_provider.cpp
  historical_speed_provider.hpp
  historical_traffic_provider.cpp
  historical_traffic_provider.hpp
  osm_speed_inference.cpp
  osm_speed_inference.hpp
  speed_groups.cpp
  speed_groups.hpp
  traffic_cache.cpp
  traffic_cache.hpp
  traffic_estimator.cpp
  traffic_estimator.hpp
  traffic_info.cpp
  traffic_info.hpp
)

omim_add_library(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    routing_common
    indexer
)

omim_add_test_subdirectory(traffic_tests)
