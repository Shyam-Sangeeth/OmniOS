# The Phase 10 QML shell. Reached only when Qt6 was found by the root
# CMakeLists, so a host without Qt6 still builds the core, omnictl and tests.

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(omni-launcher
    main.cpp
    GameListModel.cpp
    AppListModel.cpp
    HyprlandEvents.cpp
    GamepadInput.cpp
    SystemStatus.cpp
    LauncherController.cpp
    InstallerController.cpp
    GreeterController.cpp
    KeyDelivery.cpp)

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
        MenuPanel.qml
        OmniLogo.qml
        StatusButton.qml
        GameDetail.qml
        InstallerWindow.qml
        GreeterWindow.qml
        ActionButton.qml
        Field.qml
    SOURCES
        GameListModel.h
        AppListModel.h
        HyprlandEvents.h
        GamepadInput.h
        SystemStatus.h
        LauncherController.h
        InstallerController.h
        GreeterController.h
        KeyDelivery.h)

# SDL3 for the controller. Optional on purpose: without it the shell still
# builds and still works, it is simply keyboard-only. Qt6 has no gamepad module
# of its own — QtGamepad did not survive the move from Qt5 — so there is no
# in-tree alternative to fall back on.
find_package(SDL3 QUIET)
if(SDL3_FOUND)
    message(STATUS "SDL3 found - the launcher will accept a controller")
    target_compile_definitions(omni-launcher PRIVATE OMNIOS_HAS_GAMEPAD)
    target_link_libraries(omni-launcher PRIVATE SDL3::SDL3)
else()
    message(WARNING "SDL3 not found - the launcher will be keyboard-only")
endif()

target_link_libraries(omni-launcher PRIVATE
    omnios_core
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    # QLocalSocket, for the sign-in screen's conversation with greetd.
    Qt6::Network)

target_include_directories(omni-launcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT MSVC)
    target_compile_options(omni-launcher PRIVATE -Wall -Wextra)
endif()
