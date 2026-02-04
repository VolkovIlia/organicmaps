project(mesh)

set(SRC
  # BLE constants and common types
  ble_constants.cpp
  ble_constants.hpp

  # BLE protocol
  ble_protocol.cpp
  ble_protocol.hpp

  # Connection management
  connection_manager.cpp
  connection_manager.hpp

  # P2P coordination
  opportunistic_share.cpp
  opportunistic_share.hpp

  # Platform abstraction (interface headers only)
  ble_advertiser.hpp
  ble_scanner.hpp
)

omim_add_library(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    base
    p2p
)

omim_add_test_subdirectory(mesh_tests)
