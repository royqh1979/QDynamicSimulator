add_library(qds-editor STATIC
    DiagramItems.cpp
    DiagramItems.h
    DiagramScene.cpp
    DiagramScene.h
    DiagramView.cpp
    DiagramView.h
    DocumentController.cpp
    DocumentController.h
    EquationEditor.cpp
    EquationEditor.h
    MainWindow.cpp
    MainWindow.h
    MiniMapView.cpp
    MiniMapView.h
    RecentFiles.cpp
    RecentFiles.h
    Routing.cpp
    Routing.h
)
add_library(QDS::Editor ALIAS qds-editor)

target_include_directories(qds-editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qds-editor
    PUBLIC
        QDS::Core
        QDS::IO
        Qt5::Widgets
)

add_executable(qdynamic-simulator WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(qdynamic-simulator PRIVATE QDS::Editor)
