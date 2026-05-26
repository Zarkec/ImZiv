cmake_minimum_required(VERSION 3.16)

set(IMZIV_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${IMZIV_VERSION_FILE}")
file(READ "${IMZIV_VERSION_FILE}" IMZIV_VERSION)
string(STRIP "${IMZIV_VERSION}" IMZIV_VERSION)
string(REPLACE ".WIP" "" IMZIV_VERSION_PLAIN "${IMZIV_VERSION}")

project(ImZiv VERSION ${IMZIV_VERSION_PLAIN} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(IMZIV_PACKAGE_ARCH "x86_64")
else()
    set(IMZIV_PACKAGE_ARCH "x86")
endif()

# -------------------------------------------------
# OpenCV
# -------------------------------------------------
set(OpenCV_DIR "D:/Project/opencv-mingw/x64/mingw/lib" CACHE PATH "OpenCV lib directory")
find_package(OpenCV REQUIRED)

# -------------------------------------------------
# GLFW (auto-download via FetchContent)
# -------------------------------------------------
include(FetchContent)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# -------------------------------------------------
# ImGui (local docking branch copied from ImHex)
# -------------------------------------------------
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui)
set(IMGUI_BACKEND_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui/backend)
set(IMNODES_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui/imnodes)
set(NLOHMANN_JSON_DIR ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/single_include)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/source/imgui.cpp
    ${IMGUI_DIR}/source/imgui_demo.cpp
    ${IMGUI_DIR}/source/imgui_draw.cpp
    ${IMGUI_DIR}/source/imgui_tables.cpp
    ${IMGUI_DIR}/source/imgui_widgets.cpp
    ${IMGUI_BACKEND_DIR}/source/imgui_impl_glfw.cpp
    ${IMGUI_BACKEND_DIR}/source/imgui_impl_opengl3.cpp
    ${IMNODES_DIR}/source/imnodes.cpp
)

set(WIN_RESOURCES)
if(WIN32)
    set(WIN_RESOURCES ${CMAKE_SOURCE_DIR}/resources/app.rc)
endif()

# -------------------------------------------------
# Executable
# -------------------------------------------------
add_executable(ImZiv WIN32
    src/app/app.cpp
    src/app/ui.cpp
    src/core/content_registry.cpp
    src/core/debug.cpp
    src/core/imgui_extensions.cpp
    src/core/task_manager.cpp
    src/core/unlocalized_string.cpp
    src/core/view.cpp
    src/core/view_tools.cpp
    src/workflow/image_workflow.cpp
    src/workflow/image_workflow_nodes.cpp
    src/workflow/nodes/input.cpp
    src/workflow/nodes/geometry.cpp
    src/workflow/nodes/color.cpp
    src/workflow/nodes/color_adjust.cpp
    src/workflow/nodes/threshold.cpp
    src/workflow/nodes/filter.cpp
    src/workflow/nodes/edge.cpp
    src/workflow/nodes/analysis.cpp
    src/workflow/nodes/output.cpp
    src/viewer/image_canvas.cpp
    src/main.cpp
    src/tools/measurement_tool.cpp
    src/tools/angle_measurement_tool.cpp
    src/tools/color_picker_tool.cpp
    src/platform/native_dialog.cpp
    src/platform/platform_window.cpp
    src/viewer/view_image_editor.cpp
    src/workflow/view_image_workflow.cpp
    ${WIN_RESOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(ImZiv PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}/include
    ${IMGUI_BACKEND_DIR}/include
    ${IMNODES_DIR}/include
    ${NLOHMANN_JSON_DIR}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(ImZiv PRIVATE
    glfw
    ${OpenCV_LIBS}
    opengl32
    dwmapi
    ole32
    shell32
    winhttp
    winmm
)

# -------------------------------------------------
# Runtime DLL search paths (Windows)
# -------------------------------------------------
if(WIN32)
    set(IMZIV_OPENCV_BIN_CANDIDATES
        "${OpenCV_DIR}/../bin"
        "${OpenCV_DIR}/../../bin"
        "${OpenCV_DIR}/../../../bin"
    )

    get_filename_component(IMZIV_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
endif()

add_custom_command(TARGET ImZiv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:ImZiv>/assets"
)

include(GNUInstallDirs)

if(WIN32)
    # Keep the Windows portable package flat, like ImHex's install artifact.
    set(CMAKE_INSTALL_BINDIR ".")
endif()

install(TARGETS ImZiv
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets"
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(WIN32)
    install(CODE "set(IMZIV_RUNTIME_DIRS \"${IMZIV_OPENCV_BIN_CANDIDATES};${IMZIV_COMPILER_BIN_DIR}\")")
    install(CODE "set(IMZIV_INSTALL_BINDIR \"${CMAKE_INSTALL_BINDIR}\")")
    install(CODE [[
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES "$<TARGET_FILE:ImZiv>"
            RESOLVED_DEPENDENCIES_VAR _r_deps
            UNRESOLVED_DEPENDENCIES_VAR _u_deps
            CONFLICTING_DEPENDENCIES_PREFIX _c_deps
            DIRECTORIES ${IMZIV_RUNTIME_DIRS} $ENV{PATH}
            POST_EXCLUDE_REGEXES
                ".*[Ss]ystem32/.*\\.dll"
                ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/.*\\.dll"
        )

        if(_u_deps)
            message(WARNING "Unresolved runtime dependencies: ${_u_deps}")
        endif()

        if(_c_deps_FILENAMES AND _c_deps AND NOT (_c_deps STREQUAL ""))
            message(WARNING "Conflicting runtime dependencies: ${_c_deps}")
        endif()

        foreach(_file ${_r_deps})
            file(INSTALL
                DESTINATION "${CMAKE_INSTALL_PREFIX}/${IMZIV_INSTALL_BINDIR}"
                TYPE SHARED_LIBRARY
                FOLLOW_SYMLINK_CHAIN
                FILES "${_file}"
            )
        endforeach()
    ]])
endif()

set(CPACK_PACKAGE_NAME "ImZiv")
set(CPACK_PACKAGE_VENDOR "imziv")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${IMZIV_VERSION}-Windows-${IMZIV_PACKAGE_ARCH}")

include(CPack)
