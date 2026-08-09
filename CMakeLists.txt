cmake_minimum_required(VERSION 3.25)

project(ScratchEditor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick)

qt_standard_project_setup(REQUIRES 6.8)

add_library(ScratchEditorCjkText STATIC
    src/cjktextprocessor.cpp
    src/cjktextprocessor.h
)

target_include_directories(ScratchEditorCjkText PUBLIC src)
target_link_libraries(ScratchEditorCjkText PUBLIC Qt6::Core)

qt_add_executable(ScratchEditor WIN32
    src/main.cpp
    src/appsettings.cpp
    src/appsettings.h
    src/editorcontroller.cpp
    src/editorcontroller.h
    src/externalfilesession.cpp
    src/externalfilesession.h
    src/editorcommandregistry.cpp
    src/editorcommandregistry.h
    src/markdownhighlighter.cpp
    src/markdownhighlighter.h
    src/markdownstyle.cpp
    src/markdownstyle.h
    src/statuspanelhints.h
    src/windowplacement.cpp
    src/windowplacement.h
    src/clipboardgateway.cpp
    src/clipboardgateway.h
    src/clipboardhistorycommandgate.h
    src/clipboardhistorymodel.cpp
    src/clipboardhistorymodel.h
    src/clipboardhistorystore.cpp
    src/clipboardhistorystore.h
)

set(SCRATCHEDITOR_MARKDOWN_STYLE
    "${CMAKE_CURRENT_SOURCE_DIR}/config/markdown-style.json"
)

add_custom_command(TARGET ScratchEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ScratchEditor>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SCRATCHEDITOR_MARKDOWN_STYLE}"
            "$<TARGET_FILE_DIR:ScratchEditor>/config/markdown-style.json"
    VERBATIM
)

qt_add_qml_module(ScratchEditor
    URI ScratchEditor
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_link_libraries(ScratchEditor PRIVATE
    ScratchEditorCjkText
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
)

target_link_options(ScratchEditor PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-s>
)

if(WIN32)
    target_compile_definitions(ScratchEditor PRIVATE UNICODE _UNICODE)
    target_link_libraries(ScratchEditor PRIVATE crypt32 dwmapi user32)
endif()

qt_add_executable(ScratchEditorClipboardHistoryTests
    tests/clipboardhistory_main.cpp
    src/clipboardgateway.cpp
    src/clipboardgateway.h
    src/clipboardhistorycommandgate.h
    src/clipboardhistorymodel.cpp
    src/clipboardhistorymodel.h
    src/clipboardhistorystore.cpp
    src/clipboardhistorystore.h
)

target_include_directories(ScratchEditorClipboardHistoryTests PRIVATE src)
target_link_libraries(ScratchEditorClipboardHistoryTests PRIVATE Qt6::Core)
if(WIN32)
    target_compile_definitions(ScratchEditorClipboardHistoryTests PRIVATE UNICODE _UNICODE)
    target_link_libraries(ScratchEditorClipboardHistoryTests PRIVATE crypt32 user32)
endif()

qt_add_executable(ScratchEditorEditingTests
    tests/editing_main.cpp
)

qt_add_executable(ScratchEditorWindowUiTests
    tests/window_ui_main.cpp
    src/windowplacement.cpp
    src/windowplacement.h
)

target_include_directories(ScratchEditorWindowUiTests PRIVATE src)

target_link_libraries(ScratchEditorWindowUiTests PRIVATE
    Qt6::Core
    Qt6::Network
)

target_link_options(ScratchEditorWindowUiTests PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-s>
)

target_link_libraries(ScratchEditorEditingTests PRIVATE
    ScratchEditorCjkText
    Qt6::Core
    Qt6::Network
)

target_link_options(ScratchEditorEditingTests PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-s>
)

qt_add_executable(ScratchEditorPerf
    tests/perf_main.cpp
)

target_link_libraries(ScratchEditorPerf PRIVATE
    Qt6::Core
    Qt6::Network
)


target_link_options(ScratchEditorPerf PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-s>
)

if(WIN32)
    target_compile_definitions(ScratchEditorPerf PRIVATE UNICODE _UNICODE)
    target_link_libraries(ScratchEditorPerf PRIVATE user32)
endif()

qt_add_executable(ScratchEditorSystemTests
    tests/system_main.cpp
)

target_link_libraries(ScratchEditorSystemTests PRIVATE
    Qt6::Core
    Qt6::Network
)

target_link_options(ScratchEditorSystemTests PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-s>
)

if(WIN32)
    target_compile_definitions(ScratchEditorSystemTests PRIVATE UNICODE _UNICODE)
    target_link_libraries(ScratchEditorSystemTests PRIVATE ole32 user32)
endif()

qt_add_executable(ScratchEditorExternalFileTests
    tests/externalfilesession_main.cpp
    src/externalfilesession.cpp
    src/externalfilesession.h
)

qt_add_executable(ScratchEditorExternalProcessTests
    tests/externaleditorprocess_main.cpp
)

target_include_directories(ScratchEditorExternalProcessTests PRIVATE src)

target_include_directories(ScratchEditorExternalFileTests PRIVATE src)

target_link_libraries(ScratchEditorExternalFileTests PRIVATE
    Qt6::Core
)

target_link_libraries(ScratchEditorExternalProcessTests PRIVATE
    Qt6::Core
)

target_link_options(ScratchEditorExternalFileTests PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-s>
)

target_link_options(ScratchEditorExternalProcessTests PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-s>
)

install(TARGETS ScratchEditor ScratchEditorPerf ScratchEditorSystemTests ScratchEditorEditingTests
        ScratchEditorClipboardHistoryTests
        ScratchEditorWindowUiTests ScratchEditorExternalFileTests ScratchEditorExternalProcessTests
    RUNTIME DESTINATION .
)

install(FILES "${SCRATCHEDITOR_MARKDOWN_STYLE}" DESTINATION config)
