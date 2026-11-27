add_executable(test-editor
    TestEditor.cpp
)
target_link_libraries(test-editor PRIVATE QDS::Editor Qt5::Test)
target_compile_definitions(test-editor PRIVATE QDS_SIR_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/samples/SIR/SIR.xmile")
target_compile_definitions(test-editor PRIVATE QDS_MOVE_UNDO_QDS_PATH="${CMAKE_SOURCE_DIR}/tests/data/move-undo.qds")
target_compile_definitions(test-editor PRIVATE QDS_ZH_CN_TS_PATH="${CMAKE_SOURCE_DIR}/translations/qds_zh_CN.ts")
target_compile_definitions(test-editor PRIVATE QDS_TEACUP_MDL_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/samples/teacup/teacup.mdl")
add_test(NAME editor-commands COMMAND test-editor)
add_dependencies(all-test-targets test-editor)

add_executable(test-simulation-controller
    TestSimulationController.cpp
)
target_link_libraries(test-simulation-controller PRIVATE QDS::Editor Qt5::Test)
add_test(NAME editor-simulation-controller COMMAND test-simulation-controller)
add_dependencies(all-test-targets test-simulation-controller)

add_executable(test-icons
    TestIcons.cpp
    ../../src/app/resources/icons.qrc
)
target_link_libraries(test-icons PRIVATE Qt5::Widgets Qt5::Test Qt5::Svg)
add_test(NAME icons-resources COMMAND test-icons)
add_dependencies(all-test-targets test-icons)
