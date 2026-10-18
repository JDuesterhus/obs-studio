add_executable(test_replay_buffer_window test_replay_buffer_window.c)
target_include_directories(
  test_replay_buffer_window
  PRIVATE "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg"
)
set_target_properties(test_replay_buffer_window PROPERTIES FOLDER "Tests and Examples")
add_test(NAME test_replay_buffer_window COMMAND test_replay_buffer_window)
