add_library(qds-editor STATIC
    DiagramItems.cpp
    DiagramItems.h
    DiagramPalette.h
    DiagramScene.cpp
    DiagramScene.h
    DiagramView.cpp
    DiagramView.h
    CausesStripWidget.cpp
    CausesStripWidget.h
    DocumentController.cpp
    DocumentController.h
    SimulationCauses.cpp
    SimulationCauses.h
    EntityPalette.cpp
    EntityPalette.h
    EquationEditor.cpp
    EquationEditor.h
    MainWindow.cpp
    MainWindow.h
    MiniMapView.cpp
    MiniMapView.h
    PropertyPanel.cpp
    PropertyPanel.h
    RunsCompareModel.cpp
    RunsCompareModel.h
    RecentFiles.cpp
    RecentFiles.h
    Routing.cpp
    Routing.h
    SimulationController.cpp
    SimulationController.h
    SimulationResultsWidget.cpp
    SimulationResultsWidget.h
    SimulationSettingsDialog.cpp
    SimulationTableModel.cpp
    SimulationTableModel.h
    SimulationSettingsDialog.h
    Translation.cpp
    Translation.h
    TimeSeriesChartWidget.cpp
    TimeSeriesChartWidget.h
    resources/icons.qrc
)
add_library(QDS::Editor ALIAS qds-editor)

qt5_create_translation(QDS_QM_FILES ${CMAKE_SOURCE_DIR}/src/app ${CMAKE_SOURCE_DIR}/translations/qds_zh_CN.ts)
set(QDS_I18N_QRC ${CMAKE_CURRENT_BINARY_DIR}/i18n.qrc)
file(WRITE ${QDS_I18N_QRC} "<RCC><qresource prefix=\"/i18n\"><file alias=\"qds_zh_CN.qm\">${CMAKE_CURRENT_BINARY_DIR}/qds_zh_CN.qm</file></qresource></RCC>")
qt5_add_resources(QDS_I18N_RCC ${QDS_I18N_QRC})
set_property(SOURCE ${QDS_I18N_RCC} PROPERTY OBJECT_DEPENDS ${QDS_QM_FILES})
target_sources(qds-editor PRIVATE ${QDS_I18N_RCC})

target_include_directories(qds-editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qds-editor
    PUBLIC
        QDS::Core
        QDS::IO
        Qt5::Widgets
        Qt5::Svg
        Qt5::Charts
)

add_executable(qdynamic-simulator WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(qdynamic-simulator PRIVATE QDS::Editor)
