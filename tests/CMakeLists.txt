add_executable(omnios_tests
    main.cpp
    AppsTest.cpp
    DesktopEntryTest.cpp
    DetectorTest.cpp
    JsonTest.cpp
    LibraryTest.cpp
    ManifestTest.cpp
    RouterTest.cpp
    ScannerTest.cpp
    SteamLibraryTest.cpp
    IptvTest.cpp
    EpgTest.cpp
    EmulatorSetupTest.cpp
    KeyboardLayoutTest.cpp
    CoverArtTest.cpp)

target_link_libraries(omnios_tests PRIVATE omnios_core)
target_include_directories(omnios_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT MSVC)
    target_compile_options(omnios_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME omnios_tests COMMAND omnios_tests)
