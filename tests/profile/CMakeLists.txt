add_executable(profile-core
    CoreProfile.cpp
)
target_link_libraries(profile-core PRIVATE QDS::Core Qt5::Core)
add_dependencies(all-profile-targets profile-core)

add_executable(profile-editor
    EditorProfile.cpp
)
target_link_libraries(profile-editor PRIVATE QDS::Editor Qt5::Widgets)
add_dependencies(all-profile-targets profile-editor)
