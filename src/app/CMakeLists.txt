set(CMAKE_AUTOMOC ON)

qt_add_executable(seamforge
    main.cpp
    AppState.cpp
    MainWindow.cpp
    Viewport3D.cpp
    PatternView.cpp
)
target_link_libraries(seamforge PRIVATE
    seamforge_core
    Qt6::Widgets
    Qt6::OpenGLWidgets
)
find_package(OpenGL REQUIRED)
target_link_libraries(seamforge PRIVATE OpenGL::GL)
