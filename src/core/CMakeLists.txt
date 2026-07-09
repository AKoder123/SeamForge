add_library(seamforge_core STATIC
    Mesh.cpp
    Validation.cpp
    Io.cpp
    Curvature.cpp
    Seam.cpp
    Segmentation.cpp
    Flatten.cpp
    Relations.cpp
    Matching.cpp
    Regularize.cpp
    Export.cpp
    Project.cpp
    Procedural.cpp
)
target_include_directories(seamforge_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(seamforge_core PUBLIC
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    assimp::assimp
)
target_compile_options(seamforge_core PRIVATE -Wall -Wextra)
