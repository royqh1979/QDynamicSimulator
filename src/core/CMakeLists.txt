add_library(qds-core STATIC
    include/core/ChangeSet.h
    include/core/Diagram.h
    include/core/Dimension.h
    include/core/Document.h
    include/core/Element.h
    include/core/ElementRange.h
    include/core/formula/Analysis.h
    include/core/formula/Evaluation.h
    include/core/formula/Formula.h
    include/core/formula/Function.h
    include/core/Identifier.h
    include/core/Macro.h
    include/core/Model.h
    include/core/Validation.h
    src/Diagram.cpp
    src/Dimension.cpp
    src/Document.cpp
    src/formula/Analysis.cpp
    src/formula/Evaluation.cpp
    src/formula/Formula.cpp
    src/formula/Function.cpp
    src/Identifier.cpp
    src/Model.cpp
    src/Validation.cpp
)
add_library(QDS::Core ALIAS qds-core)

target_compile_features(qds-core PUBLIC cxx_std_17)

target_include_directories(qds-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
