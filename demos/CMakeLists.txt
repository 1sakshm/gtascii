add_executable(terminal_demo
    terminal_demo/main.cpp
    terminal_demo/DemoOptions.cpp
    terminal_demo/Hud.cpp
    terminal_demo/PatternUtil.cpp
    terminal_demo/patterns/Interference.cpp
    terminal_demo/patterns/Plasma.cpp
    terminal_demo/patterns/Shapes.cpp
    terminal_demo/patterns/TestCard.cpp
    terminal_demo/patterns/Tunnel.cpp
)

target_include_directories(terminal_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/terminal_demo)
target_link_libraries(terminal_demo PRIVATE av_engine)
av_configure_executable(terminal_demo)

add_executable(mesh_demo mesh_demo/main.cpp)
target_link_libraries(mesh_demo PRIVATE av_engine)
av_configure_executable(mesh_demo)
