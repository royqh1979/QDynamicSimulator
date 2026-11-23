add_library(qds-core STATIC
    include/qds/core/ChangeSet.h
    include/qds/core/Document.h
    include/qds/core/Model.h
    src/Document.cpp
    src/Model.cpp
)
add_library(QDS::Core ALIAS qds-core)

target_compile_features(qds-core PUBLIC cxx_std_17)

target_include_directories(qds-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
