find_package(Qt5 5.12 QUIET COMPONENTS Zlib)

add_executable(test-xmile
    TestXmileFile.cpp
)
target_link_libraries(test-xmile PRIVATE QDS::IO Qt5::Test)
target_compile_definitions(test-xmile PRIVATE QDS_SIR_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/samples/SIR/SIR.xmile")
if(TARGET Qt5::Zlib)
    target_link_libraries(test-xmile PRIVATE Qt5::Zlib)
else()
    find_package(ZLIB REQUIRED)
    target_link_libraries(test-xmile PRIVATE ZLIB::ZLIB)
endif()
add_test(NAME io-xmile COMMAND test-xmile)
add_dependencies(all-test-targets test-xmile)

if(QDS_ENABLE_XMILE_CORPUS_TESTS)
    add_executable(test-xmile-corpus TestXmileCorpus.cpp)
    target_link_libraries(test-xmile-corpus PRIVATE QDS::IO Qt5::Test)
    target_compile_definitions(test-xmile-corpus PRIVATE QDS_XMILE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/3rdparty/test-models")
    add_test(NAME io-xmile-corpus COMMAND test-xmile-corpus)
    add_dependencies(all-test-targets test-xmile-corpus)
endif()
