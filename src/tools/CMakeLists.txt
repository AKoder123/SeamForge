add_executable(seamforge-cli seamforge_cli.cpp)
target_link_libraries(seamforge-cli PRIVATE seamforge_core)

add_executable(make-test-meshes make_test_meshes.cpp)
target_link_libraries(make-test-meshes PRIVATE seamforge_core)
