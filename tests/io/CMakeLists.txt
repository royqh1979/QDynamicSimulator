find_package(Qt5 5.12 QUIET COMPONENTS Zlib)

add_executable(test-xmile
    TestXmileFile.cpp
)
target_link_libraries(test-xmile PRIVATE QDS::IO Qt5::Test)
if(TARGET Qt5::Zlib)
    target_link_libraries(test-xmile PRIVATE Qt5::Zlib)
else()
    find_package(ZLIB REQUIRED)
    target_link_libraries(test-xmile PRIVATE ZLIB::ZLIB)
endif()
add_test(NAME io-xmile COMMAND test-xmile)
add_dependencies(all-test-targets test-xmile)
