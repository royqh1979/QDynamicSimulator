add_executable(test-editor
    TestEditor.cpp
)
target_link_libraries(test-editor PRIVATE QDS::Editor Qt5::Test)
add_test(NAME editor-commands COMMAND test-editor)
add_dependencies(all-test-targets test-editor)
