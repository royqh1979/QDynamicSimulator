add_executable(profile-core
    CoreProfile.cpp
)
target_link_libraries(profile-core PRIVATE QDS::Core Qt5::Core)
add_dependencies(all-profile-targets profile-core)

add_executable(profile-formula
    FormulaProfile.cpp
)
target_link_libraries(profile-formula PRIVATE QDS::Core Qt5::Core)
add_dependencies(all-profile-targets profile-formula)

add_executable(profile-simulation
    SimulationProfile.cpp
)
target_link_libraries(profile-simulation PRIVATE QDS::IO Qt5::Core)
add_dependencies(all-profile-targets profile-simulation)

add_executable(profile-delay
    DelayProfile.cpp
)
target_link_libraries(profile-delay PRIVATE QDS::Core Qt5::Core)
add_dependencies(all-profile-targets profile-delay)

add_executable(profile-array
    ArrayProfile.cpp
)
target_link_libraries(profile-array PRIVATE QDS::Core Qt5::Core)
add_dependencies(all-profile-targets profile-array)

add_executable(random-samples
    RandomSamples.cpp
)
target_link_libraries(random-samples PRIVATE QDS::Core Qt5::Core)
add_dependencies(all-profile-targets random-samples)

add_executable(profile-mdl
    MdlProfile.cpp
)
target_link_libraries(profile-mdl PRIVATE QDS::IO Qt5::Core)
target_compile_definitions(profile-mdl PRIVATE
    QDS_ABS_MDL_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/tests/abs/test_abs.mdl"
    QDS_TEACUP_MDL_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/samples/teacup/teacup.mdl"
    QDS_WFINV_MDL_PATH="${CMAKE_SOURCE_DIR}/tests/data/wfinv.mdl"
)
add_dependencies(all-profile-targets profile-mdl)

add_executable(profile-editor
    EditorProfile.cpp
)
target_link_libraries(profile-editor PRIVATE QDS::Editor Qt5::Widgets)
target_compile_definitions(profile-editor PRIVATE QDS_SIR_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/samples/SIR/SIR.xmile")
target_compile_definitions(profile-editor PRIVATE QDS_MOVE_UNDO_QDS_PATH="${CMAKE_SOURCE_DIR}/tests/data/move-undo.qds")
add_dependencies(all-profile-targets profile-editor)

add_executable(profile-icons
    IconProfile.cpp
    ../../src/app/resources/icons.qrc
)
target_link_libraries(profile-icons PRIVATE Qt5::Widgets Qt5::Svg)
add_dependencies(all-profile-targets profile-icons)
