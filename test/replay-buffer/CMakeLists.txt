add_executable(test_replay_buffer_window test_replay_buffer_window.c)
target_include_directories(test_replay_buffer_window PRIVATE "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg")
set_target_properties(test_replay_buffer_window PROPERTIES FOLDER "Tests and Examples")
add_test(NAME test_replay_buffer_window COMMAND test_replay_buffer_window)

add_executable(
  test_replay_buffer_disk
  test_replay_buffer_disk.c
  "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg/replay-buffer-disk.c"
)
target_include_directories(test_replay_buffer_disk PRIVATE "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg")
target_link_libraries(test_replay_buffer_disk PRIVATE OBS::libobs)
set_target_properties(test_replay_buffer_disk PROPERTIES FOLDER "Tests and Examples")
add_test(NAME test_replay_buffer_disk COMMAND test_replay_buffer_disk)
if(WIN32)
  set(
    _replay_test_path
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:libobs>;PATH=path_list_prepend:${OBS_OUTPUT_DIR}/$<CONFIG>/bin/64bit"
  )
else()
  set(_replay_test_path "PATH=path_list_prepend:$<TARGET_FILE_DIR:libobs>")
endif()
set_tests_properties(test_replay_buffer_disk PROPERTIES ENVIRONMENT_MODIFICATION "${_replay_test_path}")

add_executable(
  test_replay_buffer_disk_nospace
  test_replay_buffer_disk_nospace.c
  "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg/replay-buffer-disk.c"
)
target_include_directories(test_replay_buffer_disk_nospace PRIVATE "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg")
target_compile_definitions(test_replay_buffer_disk_nospace PRIVATE REPLAY_DISK_MIN_FREE_SPACE=1099511627776LL)
target_link_libraries(test_replay_buffer_disk_nospace PRIVATE OBS::libobs)
set_target_properties(test_replay_buffer_disk_nospace PROPERTIES FOLDER "Tests and Examples")
add_test(NAME test_replay_buffer_disk_nospace COMMAND test_replay_buffer_disk_nospace)
set_tests_properties(test_replay_buffer_disk_nospace PROPERTIES ENVIRONMENT_MODIFICATION "${_replay_test_path}")

add_executable(
  test_replay_buffer_disk_slow
  test_replay_buffer_disk_slow.c
  "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg/replay-buffer-disk.c"
)
target_include_directories(test_replay_buffer_disk_slow PRIVATE "${CMAKE_SOURCE_DIR}/plugins/obs-ffmpeg")
target_compile_definitions(test_replay_buffer_disk_slow PRIVATE REPLAY_DISK_QUEUE_LIMIT=1)
target_link_libraries(test_replay_buffer_disk_slow PRIVATE OBS::libobs)
set_target_properties(test_replay_buffer_disk_slow PROPERTIES FOLDER "Tests and Examples")
add_test(NAME test_replay_buffer_disk_slow COMMAND test_replay_buffer_disk_slow)
set_tests_properties(test_replay_buffer_disk_slow PROPERTIES ENVIRONMENT_MODIFICATION "${_replay_test_path}")
