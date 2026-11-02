cmake_minimum_required(VERSION 3.16)
project(UniEmu CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(IMGUI third_party/imgui)
add_executable(UniEmu
  src/main.cpp src/launcher.cpp src/play.cpp src/core.cpp src/video.cpp src/audio.cpp
  src/input.cpp src/detect.cpp src/settings.cpp src/util.cpp src/gl.cpp src/shaders_xbr.cpp src/controllers.cpp src/pad_ui.cpp src/http.cpp src/covers.cpp src/texcache.cpp src/stb_impl.cpp src/library_ui.cpp src/gpu.cpp src/external.cpp src/shaders_fsr.cpp src/fsr_cpu.c src/appicon.cpp
  ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
  ${IMGUI}/backends/imgui_impl_sdl2.cpp ${IMGUI}/backends/imgui_impl_opengl3.cpp
  third_party/miniz/miniz.c)
target_include_directories(UniEmu PRIVATE src ${IMGUI} ${IMGUI}/backends third_party/miniz third_party)
target_compile_definitions(UniEmu PRIVATE UNIEMU_VERSION="2.1.0" MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

if(WIN32)
  set(SDL2_ROOT "" CACHE PATH "SDL2 mingw dev root (x86_64-w64-mingw32)")
  target_include_directories(UniEmu PRIVATE ${SDL2_ROOT}/include/SDL2)
  target_link_directories(UniEmu PRIVATE ${SDL2_ROOT}/lib)
  target_sources(UniEmu PRIVATE res/uniemu.rc)
  set_source_files_properties(res/uniemu.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/res/uniemu.ico;${CMAKE_SOURCE_DIR}/res/uniemu.manifest")
  target_link_libraries(UniEmu PRIVATE mingw32 SDL2main SDL2 opengl32 dinput8 dxguid user32 gdi32 winmm imm32 ole32 oleaut32 shell32 setupapi version uuid comdlg32 winhttp m)
  target_link_options(UniEmu PRIVATE -mwindows -static -static-libgcc -static-libstdc++)
else()
  find_package(SDL2 REQUIRED)
  target_include_directories(UniEmu PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(UniEmu PRIVATE ${SDL2_LIBRARIES} GL dl pthread)
endif()
