cmake_minimum_required(VERSION 3.21)
project(KonsoleWindowsBackendTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
# Match the Qt/KDE compile policy of the full application.
add_compile_definitions(QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII QT_NO_CAST_FROM_BYTEARRAY
    QT_NO_CAST_TO_ASCII QT_USE_QSTRINGBUILDER QT_NO_URL_CAST_FROM_STRING
    QT_NO_FOREACH QT_NO_NARROWING_CONVERSIONS_IN_CONNECT)
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Concurrent Test)
include(../../tools/windows/completion-header.cmake)
konsole_generate_completion_header("${CMAKE_CURRENT_BINARY_DIR}")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
enable_testing()
add_executable(WindowsProcessTest WindowsProcessTest.cpp)
target_link_libraries(WindowsProcessTest PRIVATE Qt6::Core Qt6::Test)
add_test(NAME WindowsProcessTest COMMAND WindowsProcessTest)
add_executable(SelectionClipboardTest SelectionClipboardTest.cpp)
target_link_libraries(SelectionClipboardTest PRIVATE Qt6::Gui Qt6::Test)
add_test(NAME SelectionClipboardTest COMMAND SelectionClipboardTest)
add_executable(TextFilePreviewTest TextFilePreviewTest.cpp
    ../../src/filterHotSpots/TextPreviewContent.cpp
    ../../src/filterHotSpots/TextFilePreview.cpp ../../src/filterHotSpots/TextFilePreview.h
    ../../src/filterHotSpots/FileMetadata.cpp ../../src/filterHotSpots/FileMetadata.h)
target_link_libraries(TextFilePreviewTest PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Test)
add_test(NAME TextFilePreviewTest COMMAND TextFilePreviewTest)
set_tests_properties(TextFilePreviewTest PROPERTIES TIMEOUT 60)
if(UNIX)
    set_tests_properties(TextFilePreviewTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(SelectionClipboardTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
add_executable(FileChannelTest FileChannelTest.cpp
    ../../src/fileremote/FileChannel.cpp ../../src/fileremote/FileRemoteProcess.cpp
    ../../src/ptyqt/iptyprocess.cpp)
target_link_libraries(FileChannelTest PRIVATE Qt6::Core Qt6::Test)
add_test(NAME FileChannelTest COMMAND FileChannelTest)
if(WIN32)
    add_executable(WindowsLauncher WIN32 ../../src/windows/launcher.cpp)
    set_target_properties(WindowsLauncher PROPERTIES OUTPUT_NAME Start-Konsole
        AUTOMOC OFF MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(WindowsLauncher PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(WindowsLauncher PRIVATE shell32 user32)
    add_executable(WindowsReportTest WindowsReportTest.cpp)
    target_link_libraries(WindowsReportTest PRIVATE Qt6::Core Qt6::Test)
    target_compile_definitions(WindowsReportTest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME WindowsReportTest COMMAND WindowsReportTest)
    set_tests_properties(WindowsReportTest PROPERTIES TIMEOUT 90
        ENVIRONMENT "KONSOLE_TEST_LAUNCHER=$<TARGET_FILE:WindowsLauncher>;KONSOLE_TEST_LAUNCH_PROBE=$<TARGET_FILE:NativeCompletionProbe>")
    add_executable(NativeCompletionProbe NativeCompletionProbe.cpp)
    set_target_properties(NativeCompletionProbe PROPERTIES AUTOMOC OFF
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_executable(FileRemoteHostExecutable ../../src/fileremote/hostmain.cpp
        ../../src/fileremote/FileRemoteHost.cpp ../../src/fileremote/FileChannel.cpp
        ../../src/filterHotSpots/TextPreviewContent.cpp ../../src/filterHotSpots/FileMetadata.cpp
        ../../src/ptyqt/conptyprocess.cpp ../../src/ptyqt/iptyprocess.cpp)
    set_target_properties(FileRemoteHostExecutable PROPERTIES OUTPUT_NAME konsole-file-host)
    target_link_libraries(FileRemoteHostExecutable PRIVATE Qt6::Core Qt6::Concurrent advapi32)
    target_compile_definitions(FileRemoteHostExecutable PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_executable(FileRemoteHostTest FileRemoteHostTest.cpp
        ../../src/fileremote/FileRemoteHost.cpp ../../src/fileremote/FileRemoteProcess.cpp ../../src/fileremote/FileChannel.cpp
        ../../src/filterHotSpots/TextPreviewContent.cpp ../../src/filterHotSpots/FileMetadata.cpp
        ../../src/ptyqt/conptyprocess.cpp ../../src/ptyqt/iptyprocess.cpp)
    target_link_libraries(FileRemoteHostTest PRIVATE Qt6::Core Qt6::Concurrent Qt6::Test advapi32)
    target_compile_definitions(FileRemoteHostTest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME FileRemoteHostTest COMMAND FileRemoteHostTest -o file-remote-results.xml,xml -o -,txt)
    set_tests_properties(FileRemoteHostTest PROPERTIES TIMEOUT 150 ENVIRONMENT "KONSOLE_TEST_HOST_BUILD=$<TARGET_FILE:FileRemoteHostExecutable>;KONSOLE_TEST_COMPLETION_EXE=$<TARGET_FILE:NativeCompletionProbe>")
    target_link_libraries(TextFilePreviewTest PRIVATE advapi32)
    add_executable(ConPtyTest ConPtyTest.cpp
        ../../src/ptyqt/conptyprocess.cpp ../../src/ptyqt/conptyprocess.h
        ../../src/ptyqt/iptyprocess.cpp ../../src/ptyqt/iptyprocess.h)
    target_link_libraries(ConPtyTest PRIVATE Qt6::Core Qt6::Test)
    target_compile_definitions(ConPtyTest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME ConPtyTest COMMAND ConPtyTest -o conpty-results.xml,xml -o -,txt)
    set_tests_properties(ConPtyTest PROPERTIES TIMEOUT 120)
endif()
