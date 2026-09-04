# The Phase 10 QML shell. Reached only when Qt6 was found by the root
# CMakeLists, so a host without Qt6 still builds the core, omnictl and tests.

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(omni-launcher
    main.cpp
    GameListModel.cpp
    LauncherController.cpp)

# The QML forms a module named "omnios". The files sit beside this file rather
# than in a qml/ subdirectory: types in a subdirectory are not registered as
# module types, so GameTile and the Theme singleton would not resolve by name.
#
# Theme.qml must be marked a singleton before qt_add_qml_module sees it.
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(omni-launcher
    URI omnios
    VERSION 1.0
    QML_FILES
        Main.qml
        Theme.qml
        GameTile.qml
        GameDetail.qml
    SOURCES
        GameListModel.h
        LauncherController.h)

target_link_libraries(omni-launcher PRIVATE
    omnios_core
    Qt6::Core
    Qt6::Gui
    Qt6::Quick)

target_include_directories(omni-launcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT MSVC)
    target_compile_options(omni-launcher PRIVATE -Wall -Wextra)
endif()
