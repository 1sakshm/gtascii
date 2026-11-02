add_executable(engine_tests
    TestMain.cpp
    TestAnsiPresenter.cpp
    TestCharacter.cpp
    TestCharRamp.cpp
    TestColor.cpp
    TestDraw2D.cpp
    TestGameplay.cpp
    TestFramebuffer.cpp
    TestJson.cpp
    TestMath.cpp
    TestMeshRender.cpp
    TestNetwork.cpp
    TestPresenterRoundTrip.cpp
    TestRooms.cpp
    TestCombat.cpp
    TestSimulation.cpp
    TestUtf8.cpp
    TestWorld.cpp
    ../client/Lobby.cpp
    ../client/NetClient.cpp
)

target_link_libraries(engine_tests PRIVATE av_server)
target_include_directories(engine_tests PRIVATE ${PROJECT_SOURCE_DIR}/client)
# Tests that read real assets (materials, maps) locate them through this.
target_compile_definitions(engine_tests PRIVATE AV_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
av_configure_executable(engine_tests)

add_test(NAME engine_tests COMMAND engine_tests)
