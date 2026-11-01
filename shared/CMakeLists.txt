# Code shared by client and server: protocol, player simulation, mission data.
add_library(av_shared STATIC
    Missions.cpp
    PlayerSim.cpp
    Protocol.cpp
)
target_include_directories(av_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av_shared PUBLIC av_engine PRIVATE av_warnings)
