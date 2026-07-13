add_executable(
  cinder_tests
  test-main.cpp
  test-stem-fading.cpp
  test-state-transitions.cpp
  test-sfx-playback.cpp
  test-effect-application.cpp)

target_link_libraries(cinder_tests PRIVATE cinder_core cimmerian)
target_include_directories(cinder_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(cinder_tests PRIVATE ${CINDER_WARNING_FLAGS})

add_test(NAME cinder_tests COMMAND cinder_tests)
