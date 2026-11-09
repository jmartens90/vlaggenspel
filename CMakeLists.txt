cmake_minimum_required(VERSION 3.16)

project(Vlaggenspel VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

qt_add_executable(vlaggenspel
    src/main.cpp
    src/flag.h src/flag.cpp
    src/world.h src/world.cpp
    src/mapview.h src/mapview.cpp
    src/flageditor.h src/flageditor.cpp
    src/mainwindow.h src/mainwindow.cpp
)

target_link_libraries(vlaggenspel PRIVATE Qt6::Widgets)

set_target_properties(vlaggenspel PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

if(ANDROID)
    set_target_properties(vlaggenspel PROPERTIES
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
        QT_ANDROID_VERSION_CODE 2
    )
endif()
