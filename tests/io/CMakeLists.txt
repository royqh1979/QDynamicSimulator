find_package(Qt5 5.12 QUIET COMPONENTS Zlib)

add_executable(test-xmile
    TestXmileFile.cpp
)
target_link_libraries(test-xmile PRIVATE QDS::IO Qt5::Test)
target_compile_definitions(test-xmile PRIVATE
    QDS_SIR_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/samples/SIR/SIR.xmile"
    QDS_SIR_RECIPROCAL_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/samples/SIR/SIR_reciprocal-dt.xmile"
)
if(TARGET Qt5::Zlib)
    target_link_libraries(test-xmile PRIVATE Qt5::Zlib)
else()
    find_package(ZLIB REQUIRED)
    target_link_libraries(test-xmile PRIVATE ZLIB::ZLIB)
endif()
add_test(NAME io-xmile COMMAND test-xmile)
add_dependencies(all-test-targets test-xmile)

add_executable(test-mdl
    TestMdlFile.cpp
)
target_link_libraries(test-mdl PRIVATE QDS::IO Qt5::Test)
target_compile_definitions(test-mdl PRIVATE
    QDS_ABS_MDL_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/abs/test_abs.mdl"
    QDS_TEACUP_MDL_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/samples/teacup/teacup.mdl"
    QDS_MACRO_MDL_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/macro_expression/test_macro_expression.mdl"
    QDS_SPECIAL_MDL_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/special_characters/test_special_variable_names.mdl"
    QDS_ARRAY_1D_MDL_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/subscript_individually_defined_1d_arrays/subscript_individually_defined_1d_arrays.mdl"
    QDS_ARRAY_2D_MDL_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/subscript_2d_arrays/test_subscript_2d_arrays.mdl"
    QDS_WFINV_MDL_PATH="${CMAKE_SOURCE_DIR}/tests/data/wfinv.mdl"
)
add_test(NAME io-mdl COMMAND test-mdl)
add_dependencies(all-test-targets test-mdl)

add_executable(test-array-files
    TestArrayFiles.cpp
)
target_link_libraries(test-array-files PRIVATE QDS::IO Qt5::Test)
target_compile_definitions(test-array-files PRIVATE
    QDS_ARRAY_APPLY_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/min_max_1arg/test_min_max_1arg.xmile"
    QDS_ARRAY_INDIVIDUAL_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/subscript_individually_defined_1d_arrays/subscript_individually_defined_1d_arrays.xmile"
)
add_test(NAME io-array-files COMMAND test-array-files)
add_dependencies(all-test-targets test-array-files)

add_executable(test-macro-files
    TestMacroFiles.cpp
)
target_link_libraries(test-macro-files PRIVATE QDS::IO Qt5::Test)
add_test(NAME io-macro-files COMMAND test-macro-files)
add_dependencies(all-test-targets test-macro-files)

add_executable(test-simulation-results
    TestSimulationResults.cpp
)
target_link_libraries(test-simulation-results PRIVATE QDS::IO Qt5::Test)
target_compile_definitions(test-simulation-results PRIVATE
    QDS_ABS_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/abs/test_abs.xmile"
    QDS_ABS_OUTPUT_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/abs/output.csv"
    QDS_EVAL_ORDER_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/eval_order/eval_order.xmile"
    QDS_EVAL_ORDER_OUTPUT_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/eval_order/output.csv"
    QDS_TEACUP_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/samples/teacup/teacup.xmile"
    QDS_TEACUP_OUTPUT_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/samples/teacup/output.csv"
    QDS_CHAINED_INIT_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/chained_initialization/test_chained_initialization.xmile"
    QDS_CHAINED_INIT_OUTPUT_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/chained_initialization/output.tab"
    QDS_SIR_MODEL_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/samples/SIR/SIR.xmile"
    QDS_SIR_MODEL_OUTPUT_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/samples/SIR/output.csv"
    QDS_LOOKUP_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/lookups/test_lookups.xmile"
    QDS_LOOKUP_OUTPUT_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/lookups/output.tab"
    QDS_LOOKUP_XSCALE_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/lookups/test_lookups_xscale.xmile"
    QDS_INLINE_LOOKUP_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/lookups_inline/test_lookups_inline.xmile"
    QDS_INLINE_LOOKUP_OUTPUT_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/lookups_inline/output.tab"
)
add_test(NAME io-simulation-results COMMAND test-simulation-results)
add_dependencies(all-test-targets test-simulation-results)

if(QDS_ENABLE_XMILE_CORPUS_TESTS)
    add_executable(test-xmile-corpus TestXmileCorpus.cpp)
    target_link_libraries(test-xmile-corpus PRIVATE QDS::IO Qt5::Test)
    target_compile_definitions(test-xmile-corpus PRIVATE QDS_XMILE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/3rdparty/test-models")
    add_test(NAME io-xmile-corpus COMMAND test-xmile-corpus)
    add_dependencies(all-test-targets test-xmile-corpus)
endif()
