include(FetchContent)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.8
  GIT_SHALLOW TRUE)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE)

FetchContent_Declare(
  nativefiledialog
  GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
  GIT_TAG v1.2.1
  GIT_SHALLOW TRUE)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_BUILD_SHELLITEMS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw imgui nlohmann_json nativefiledialog)

add_library(imgui_backend STATIC
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
target_include_directories(imgui_backend PUBLIC
  "${imgui_SOURCE_DIR}"
  "${imgui_SOURCE_DIR}/backends")
target_link_libraries(imgui_backend PUBLIC glfw)
if(APPLE)
  target_link_libraries(imgui_backend PUBLIC "-framework OpenGL")
elseif(WIN32)
  target_link_libraries(imgui_backend PUBLIC opengl32)
else()
  target_link_libraries(imgui_backend PUBLIC GL)
endif()
target_compile_features(imgui_backend PUBLIC cxx_std_17)

add_executable(prosperopkg-gui
  app/main.cpp
  app/app_state.cpp
  app/shell/window.cpp
  app/shell/top_bar.cpp
  app/shell/status_bar.cpp
  screens/home.cpp
  screens/inspect_pkg.cpp
  screens/build_pkg.cpp
  screens/pfsc_editor.cpp
  screens/key_derivation.cpp
  ui/theme.cpp
  ui/fonts.cpp
  ui/file_picker.cpp
  ui/widgets.cpp)

target_link_libraries(prosperopkg-gui PRIVATE
  prosperopkg_cpp
  imgui_backend
  glfw
  nlohmann_json::nlohmann_json
  nfd)

target_include_directories(prosperopkg-gui PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/app"
  "${CMAKE_CURRENT_SOURCE_DIR}/screens"
  "${CMAKE_CURRENT_SOURCE_DIR}/ui")

target_compile_features(prosperopkg-gui PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(prosperopkg-gui PRIVATE /W4 /permissive-)
else()
  target_compile_options(prosperopkg-gui PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(APPLE)
  set_target_properties(prosperopkg-gui PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "LibProsperoPkg"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_IDENTIFIER "com.seregonwar.libprosperopkg")
endif()
