add_executable(test-xmile
    TestXmileFile.cpp
)
target_link_libraries(test-xmile PRIVATE QDS::IO Qt5::Test)
add_test(NAME io-xmile COMMAND test-xmile)
add_dependencies(all-test-targets test-xmile)
