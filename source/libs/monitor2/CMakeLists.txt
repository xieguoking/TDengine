aux_source_directory(src MONITOR2_SRC)
add_library(monitor2 STATIC ${MONITOR2_SRC})
target_include_directories(
  monitor2
  PUBLIC "${TD_SOURCE_DIR}/include/libs/monitor2"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/inc"
)

target_link_libraries(monitor2 os util common transport)
