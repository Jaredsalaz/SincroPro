cmake_minimum_required(VERSION 3.15)
project(SincroPro CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Enable FetchContent
include(FetchContent)

# 1. Fetch GLFW
message(STATUS "Fetching GLFW...")
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# 2. Fetch Dear ImGui
message(STATUS "Fetching Dear ImGui...")
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.89.9
)
FetchContent_MakeAvailable(imgui)

# Define ImGui static library from fetched sources
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)
# ImGui needs OpenGL
if(WIN32)
    target_link_libraries(imgui PUBLIC opengl32)
else()
    target_link_libraries(imgui PUBLIC GL)
endif()

# 3. Fetch SQLite3
message(STATUS "Fetching SQLite3...")
FetchContent_Declare(
    sqlite3
    GIT_REPOSITORY https://github.com/azadkuh/sqlite-amalgamation.git
    GIT_TAG master
)
FetchContent_MakeAvailable(sqlite3)

# 4. Fetch nlohmann/json
message(STATUS "Fetching nlohmann/json...")
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json)

# Define source groups
file(GLOB_RECURSE SRC_FILES
    "src/*.cpp"
    "src/*.hpp"
    "src/*.h"
)

# Define SincroPro target
add_executable(SincroPro ${SRC_FILES})

# Include directories
target_include_directories(SincroPro PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
    ${sqlite3_SOURCE_DIR}
)

# Link Libraries
target_link_libraries(SincroPro PRIVATE
    imgui
    glfw
    SQLite3
    nlohmann_json::nlohmann_json
)

# Platform specific configurations
if(WIN32)
    # Link to ODBC and WinHTTP on Windows
    target_link_libraries(SincroPro PRIVATE odbc32 winhttp ws2_32)
    # Set linker flag for Windows Service or Windows Subsystem (hide console in Release)
    set_target_properties(SincroPro PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
else()
    # Link to ODBC and libcurl on Linux
    find_package(CURL REQUIRED)
    target_link_libraries(SincroPro PRIVATE odbc curl pthread dl)
endif()
