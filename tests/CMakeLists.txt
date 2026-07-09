add_executable(seamforge_tests
    test_mesh.cpp
    test_validation.cpp
    test_segmentation.cpp
    test_flatten.cpp
    test_relations.cpp
    test_regularize.cpp
    test_export_project.cpp
    test_propose.cpp
)
target_link_libraries(seamforge_tests PRIVATE seamforge_core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(seamforge_tests)
