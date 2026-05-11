cmake_minimum_required(VERSION 3.16)
project(cheder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(cheder
    src/main.cpp
    src/filelistmodel.h
    src/imagedir.h
    src/imageio.h
    src/imageview.cpp
    src/imageview.h
    src/infopanel.cpp
    src/infopanel.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/thumbnailcache.cpp
    src/thumbnailcache.h
    src/thumbnailview.cpp
    src/thumbnailview.h

    src/actions/action.cpp
    src/actions/action.h
    src/actions/actionbar.cpp
    src/actions/actionbar.h
    src/actions/actionlogger.h
    src/actions/actionpane.cpp
    src/actions/actionpane.h
    src/actions/actionregistry.cpp
    src/actions/actionregistry.h
    src/actions/actionwidgets.h
    src/actions/captionaction.cpp
    src/actions/captionaction.h
    src/actions/concatenateaction.cpp
    src/actions/concatenateaction.h
    src/actions/copymoveaction.cpp
    src/actions/copymoveaction.h
    src/actions/resizeaction.cpp
    src/actions/resizeaction.h
    src/actions/rotateaction.cpp
    src/actions/rotateaction.h
)
target_include_directories(cheder PRIVATE src)
target_link_libraries(cheder PRIVATE Qt6::Widgets)

include(GNUInstallDirs)
install(TARGETS cheder RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/cheder.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
