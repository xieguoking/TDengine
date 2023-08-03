aux_source_directory(src AUDIT_SRC)
add_library(audit STATIC ${AUDIT_SRC})
target_include_directories(
  audit
  PUBLIC "${TD_SOURCE_DIR}/include/libs/audit"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/inc"
)

target_link_libraries(audit os util common transport)
