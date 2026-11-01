set(AV_ENGINE_SOURCES
    AI/Navigation.cpp
    AI/PedestrianBrain.cpp
    AI/TrafficDriver.cpp
    Animation/Humanoid.cpp
    Audio/Audio.cpp
    Animation/HumanoidAnimator.cpp
    Animation/Skeleton.cpp
    Core/FileUtil.cpp
    Core/FrameClock.cpp
    Core/Json.cpp
    Core/Utf8.cpp
    Entities/VehicleVisual.cpp
    Gameplay/ThirdPersonCamera.cpp
    Graphics/AnsiPresenter.cpp
    Graphics/CharRamp.cpp
    Graphics/Draw2D.cpp
    Graphics/Framebuffer.cpp
    Graphics/Lighting.cpp
    Graphics/Material.cpp
    Graphics/Mesh.cpp
    Graphics/ObjLoader.cpp
    Graphics/Renderer.cpp
    Math/Geometry.cpp
    Math/Mat.cpp
    Physics/CharacterMotor.cpp
    Physics/CollisionWorld.cpp
    Physics/Vehicle.cpp
    Networking/Net.cpp
    Networking/WebSocket.cpp
    Platform/InputState.cpp
    World/Buildings.cpp
    World/ChunkManager.cpp
    World/CityGenerator.cpp
    World/CityLayout.cpp
    World/Props.cpp
)

if(WIN32)
    list(APPEND AV_ENGINE_SOURCES Platform/TerminalWin32.cpp)
else()
    list(APPEND AV_ENGINE_SOURCES Platform/TerminalPosix.cpp)
endif()

add_library(av_engine STATIC ${AV_ENGINE_SOURCES})

target_include_directories(av_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av_engine PRIVATE av_warnings PUBLIC Threads::Threads)

if(WIN32)
    target_compile_definitions(av_engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    # winmm: timeBeginPeriod for accurate frame pacing.
    target_link_libraries(av_engine PRIVATE winmm)
    target_link_libraries(av_engine PUBLIC ws2_32 winhttp)
endif()
if(APPLE)
    # Secure Transport for wss:// (online rooms).
    target_link_libraries(av_engine PUBLIC "-framework Security" "-framework CoreFoundation")
elseif(UNIX)
    # dlopen() of the system OpenSSL for wss://.
    target_link_libraries(av_engine PUBLIC ${CMAKE_DL_LIBS})
endif()
