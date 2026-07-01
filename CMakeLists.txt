cmake_minimum_required(VERSION 3.16)
project(cheder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

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
    src/keydispatcher.cpp
    src/keydispatcher.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/metadatareader.cpp
    src/metadatareader.h
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
    src/actions/writetarget.cpp
    src/actions/writetarget.h
    src/actions/animationaction.cpp
    src/actions/animationaction.h
    src/actions/annotateaction.cpp
    src/actions/annotateaction.h
    src/actions/captionaction.cpp
    src/actions/captionaction.h
    src/actions/concatenateaction.cpp
    src/actions/concatenateaction.h
    src/actions/copymoveaction.cpp
    src/actions/copymoveaction.h
    src/actions/cropaction.cpp
    src/actions/cropaction.h
    src/actions/gridaction.cpp
    src/actions/gridaction.h
    src/actions/renameaction.cpp
    src/actions/renameaction.h
    src/actions/resizeaction.cpp
    src/actions/resizeaction.h
    src/actions/rotateaction.cpp
    src/actions/rotateaction.h

    data/icons.qrc
)
target_include_directories(cheder PRIVATE src)
target_link_libraries(cheder PRIVATE Qt6::Widgets)

include(GNUInstallDirs)
install(TARGETS cheder RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/cheder.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# Application icon in the hicolor theme, so launchers and window managers
# resolve the "cheder" icon name from the .desktop entry.
foreach(sz 16 22 24 32 48 64 128 256)
    install(FILES data/icons/cheder-${sz}.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${sz}x${sz}/apps
        RENAME cheder.png)
endforeach()
install(FILES data/cheder.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
