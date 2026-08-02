cmake_minimum_required(VERSION 3.25)

project(ScratchEditor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(ScratchEditor WIN32
    src/main.cpp
    src/appsettings.cpp
    src/appsettings.h
    src/editorcontroller.cpp
    src/editorcontroller.h
    src/editorcommandregistry.cpp
    src/editorcommandregistry.h
    src/markdownhighlighter.cpp
    src/markdownhighlighter.h
    src/markdownstyle.cpp
    src/markdownstyle.h
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
    target_link_libraries(ScratchEditor PRIVATE dwmapi user32)
endif()

qt_add_executable(ScratchEditorStage3Tests
    tests/stage3_main.cpp
)

qt_add_executable(ScratchEditorStage4Tests
    tests/stage4_main.cpp
)

target_link_libraries(ScratchEditorStage4Tests PRIVATE
    Qt6::Core
    Qt6::Network
)

target_link_options(ScratchEditorStage4Tests PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-s>
)

target_link_libraries(ScratchEditorStage3Tests PRIVATE
    Qt6::Core
    Qt6::Network
)

target_link_options(ScratchEditorStage3Tests PRIVATE
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

qt_add_executable(ScratchEditorStage2Tests
    tests/stage2_main.cpp
)

target_link_libraries(ScratchEditorStage2Tests PRIVATE
    Qt6::Core
    Qt6::Network
)

target_link_options(ScratchEditorStage2Tests PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-s>
)

if(WIN32)
    target_compile_definitions(ScratchEditorStage2Tests PRIVATE UNICODE _UNICODE)
    target_link_libraries(ScratchEditorStage2Tests PRIVATE ole32 user32)
endif()

install(TARGETS ScratchEditor ScratchEditorPerf ScratchEditorStage2Tests ScratchEditorStage3Tests
        ScratchEditorStage4Tests
    RUNTIME DESTINATION .
)

install(FILES "${SCRATCHEDITOR_MARKDOWN_STYLE}" DESTINATION config)
