add_executable(test-editor
    TestEditor.cpp
)
target_link_libraries(test-editor PRIVATE QDS::Editor Qt5::Test)
add_test(NAME editor-commands COMMAND test-editor)
add_dependencies(all-test-targets test-editor)

add_executable(test-icons
    TestIcons.cpp
    ../../src/app/resources/icons.qrc
)
target_link_libraries(test-icons PRIVATE Qt5::Widgets Qt5::Test Qt5::Svg)
add_test(NAME icons-resources COMMAND test-icons)
add_dependencies(all-test-targets test-icons)
