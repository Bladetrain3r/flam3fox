cmake_minimum_required(VERSION 3.16)
project(flam3fox_ui LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(FLAM3_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(IMGUI_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

find_package(PkgConfig REQUIRED)
pkg_check_modules(XML2 REQUIRED libxml-2.0)
pkg_check_modules(PNG  REQUIRED libpng)
find_library(JPEG_LIB jpeg REQUIRED)
find_library(M_LIB m)

# ---- libflam3 core (compiled straight from the C sources; self-contained, no
#      autotools needed). config.h in the repo root supplies HAVE_* defines. ----
set(FLAM3_SRC
  flam3.c filters.c parser.c variations.c interpolation.c
  palettes.c jpeg.c png.c isaac.c)
list(TRANSFORM FLAM3_SRC PREPEND "${FLAM3_ROOT}/")

add_library(flam3core STATIC ${FLAM3_SRC})
target_include_directories(flam3core PUBLIC ${FLAM3_ROOT} ${XML2_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS})
target_compile_options(flam3core PRIVATE
  -O3 -ffast-math -march=native -std=gnu99 -w)
target_compile_definitions(flam3core PRIVATE
  PACKAGE_DATA_DIR="/usr/local/share/flam3" GIT_REV="flam3fox-ui")
target_link_libraries(flam3core PUBLIC
  ${XML2_LIBRARIES} ${PNG_LIBRARIES} ${JPEG_LIB} ${M_LIB} pthread)

# ---- Dear ImGui (vendored) + GLFW/OpenGL backends ----
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
pkg_check_modules(GLFW REQUIRED glfw3)
target_include_directories(imgui PUBLIC ${GLFW_INCLUDE_DIRS})

# ---- render engine (headless-testable) ----
add_library(render_engine STATIC render_engine.cpp)
target_include_directories(render_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(render_engine PUBLIC flam3core)

# ---- headless smoke test (no GUI) ----
add_executable(render_engine_test render_engine_test.cpp)
target_link_libraries(render_engine_test PRIVATE render_engine)

# ---- the GUI app ----
find_package(OpenGL REQUIRED)
add_executable(flam3fox_ui app.cpp)
target_link_libraries(flam3fox_ui PRIVATE
  render_engine imgui ${GLFW_LIBRARIES} OpenGL::GL)
target_link_directories(flam3fox_ui PRIVATE ${GLFW_LIBRARY_DIRS})
