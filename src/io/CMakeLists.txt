find_package(Qt5 5.12 QUIET COMPONENTS Zlib)

add_library(qds-io STATIC
    include/io/DocumentFile.h
    include/io/XmileFile.h
    src/DocumentFile.cpp
    src/XmileFile.cpp
)
add_library(QDS::IO ALIAS qds-io)

target_include_directories(qds-io
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(qds-io
    PUBLIC
        QDS::Core
        Qt5::Core
)

if(TARGET Qt5::Zlib)
    target_link_libraries(qds-io PRIVATE Qt5::Zlib)
else()
    find_package(ZLIB REQUIRED)
    target_link_libraries(qds-io PRIVATE ZLIB::ZLIB)
endif()
