# Authoritative game server: a library (also embedded by the client for
# single player) plus the dedicated server executable.
add_library(av_server STATIC
    ActivitySystem.cpp
    CombatSystem.cpp
    GameServer.cpp
    MissionSystem.cpp
    NpcSystem.cpp
    Persistence.cpp
    PlayerSystem.cpp
    PoliceSystem.cpp
    ServerConfig.cpp
    ServerWorld.cpp
)
target_include_directories(av_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av_server PUBLIC av_shared PRIVATE av_warnings)

add_executable(gtascii-server main.cpp)
target_link_libraries(gtascii-server PRIVATE av_server)
av_configure_executable(gtascii-server)
