add_executable(cinder_driver main.cpp)

target_link_libraries(cinder_driver PRIVATE cinder_core firefly)
target_compile_options(cinder_driver PRIVATE ${CINDER_WARNING_FLAGS})
