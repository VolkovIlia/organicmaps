project(p2p)

set(SRC
  # Privacy infrastructure
  privacy_manager.cpp
  privacy_manager.hpp
  privacy_settings.cpp
  privacy_settings.hpp
  # Rolling ID generation
  rolling_id_generator.cpp
  rolling_id_generator.hpp
)

omim_add_library(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    base
    coding
    geometry
)

omim_add_test_subdirectory(p2p_tests)
