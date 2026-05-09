cmake_minimum_required(VERSION 3.16)
project(cheder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(cheder
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/thumbnailview.cpp
    src/thumbnailview.h
    src/thumbnailcache.cpp
    src/thumbnailcache.h
    src/imageview.cpp
    src/imageview.h
    src/infopanel.cpp
    src/infopanel.h
    src/action.h
    src/actionregistry.cpp
    src/actionregistry.h
    src/actionpalette.cpp
    src/actionpalette.h
    src/actionwidgets.h
    src/rotateaction.cpp
    src/rotateaction.h
    src/resizeaction.cpp
    src/resizeaction.h
)
target_link_libraries(cheder PRIVATE Qt6::Widgets)
