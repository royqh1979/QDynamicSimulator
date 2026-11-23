add_executable(qdynamic-simulator WIN32 MACOSX_BUNDLE
    DiagramItems.cpp
    DiagramItems.h
    DiagramScene.cpp
    DiagramScene.h
    DiagramView.cpp
    DiagramView.h
    MainWindow.cpp
    MainWindow.h
    main.cpp
)

target_link_libraries(qdynamic-simulator
    PRIVATE
        QDS::Core
        QDS::IO
        Qt5::Widgets
)
