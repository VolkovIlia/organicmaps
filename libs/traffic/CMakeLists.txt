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
  personal_speed_storage.cpp
  personal_speed_storage.hpp
  speed_groups.cpp
  speed_groups.hpp
  stub_traffic_ml_model.cpp
  stub_traffic_ml_model.hpp
  traffic_cache.cpp
  traffic_cache.hpp
  traffic_deviation.cpp
  traffic_deviation.hpp
  traffic_estimator.cpp
  traffic_estimator.hpp
  traffic_info.cpp
  traffic_info.hpp
  traffic_ml_model.cpp
  traffic_ml_model.hpp
  traffic_ml_model_factory.cpp
  traffic_ml_model_factory.hpp
)

# Platform-specific ML implementations.
# TODO(mesh-traffic): Enable TFLite when TensorFlow Lite is configured
if (PLATFORM_IPHONE)
  append(SRC
    coreml_traffic_ml_model.hpp
    coreml_traffic_ml_model.mm
  )
# elseif (PLATFORM_ANDROID AND TFLITE_ENABLED)
#   append(SRC
#     tflite_traffic_ml_model.hpp
#     tflite_traffic_ml_model.cpp
#   )
endif()

omim_add_library(${PROJECT_NAME} ${SRC})

# Objective-C++ compiler options for iOS.
if (PLATFORM_IPHONE)
  target_compile_options(${PROJECT_NAME} PRIVATE -fobjc-arc)
endif()

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    routing_common
    indexer
)

# Platform-specific framework linking.
if (PLATFORM_IPHONE)
  target_link_libraries(${PROJECT_NAME}
    PUBLIC
      "-framework CoreML"
      "-framework Foundation"
  )
endif()

omim_add_test_subdirectory(traffic_tests)
