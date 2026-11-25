add_executable(test-document
    TestDocument.cpp
)
target_link_libraries(test-document PRIVATE QDS::Core Qt5::Test)
add_test(NAME core-document COMMAND test-document)
add_dependencies(all-test-targets test-document)

add_executable(test-formula
    TestFormula.cpp
)
target_link_libraries(test-formula PRIVATE QDS::Core Qt5::Test)
add_test(NAME core-formula COMMAND test-formula)
add_dependencies(all-test-targets test-formula)

add_executable(test-analysis
    TestAnalysis.cpp
)
target_link_libraries(test-analysis PRIVATE QDS::Core Qt5::Test)
add_test(NAME core-analysis COMMAND test-analysis)
add_dependencies(all-test-targets test-analysis)

add_executable(test-evaluation
    TestEvaluation.cpp
)
target_link_libraries(test-evaluation PRIVATE QDS::Core Qt5::Test)
add_test(NAME core-evaluation COMMAND test-evaluation)
add_dependencies(all-test-targets test-evaluation)

add_executable(test-arrays
    TestArrays.cpp
)
target_link_libraries(test-arrays PRIVATE QDS::Core Qt5::Test)
add_test(NAME core-arrays COMMAND test-arrays)
add_dependencies(all-test-targets test-arrays)

add_executable(test-array-evaluation
    TestArrayEvaluation.cpp
)
target_link_libraries(test-array-evaluation PRIVATE QDS::Core Qt5::Test)
add_test(NAME core-array-evaluation COMMAND test-array-evaluation)
add_dependencies(all-test-targets test-array-evaluation)
