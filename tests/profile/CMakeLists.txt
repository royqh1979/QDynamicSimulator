add_executable(profile-core
    CoreProfile.cpp
)
target_link_libraries(profile-core PRIVATE QDS::Core Qt5::Core)
add_dependencies(all-profile-targets profile-core)

add_executable(profile-editor
    EditorProfile.cpp
)
target_link_libraries(profile-editor PRIVATE QDS::Editor Qt5::Widgets)
target_compile_definitions(profile-editor PRIVATE QDS_SIR_XMILE_PATH="${CMAKE_SOURCE_DIR}/3rdparty/test-models/samples/SIR/SIR.xmile")
add_dependencies(all-profile-targets profile-editor)

add_executable(profile-icons
    IconProfile.cpp
    ../../src/app/resources/icons.qrc
)
target_link_libraries(profile-icons PRIVATE Qt5::Widgets Qt5::Svg)
add_dependencies(all-profile-targets profile-icons)
