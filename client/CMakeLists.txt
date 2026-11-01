add_executable(gtascii
    main.cpp
    ClientConfig.cpp
    Game.cpp
    GameHud.cpp
    LocalPlayer.cpp
    Lobby.cpp
    Menu.cpp
    Recorder.cpp
    NetClient.cpp
    RemoteWorld.cpp
)
# The client embeds the server library for single player and host mode.
target_link_libraries(gtascii PRIVATE av_server)
av_configure_executable(gtascii)
