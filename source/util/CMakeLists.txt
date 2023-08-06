configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/version.c.in" "${CMAKE_CURRENT_SOURCE_DIR}/src/version.c")
aux_source_directory(src UTIL_SRC)
add_library(util STATIC ${UTIL_SRC})
if (DEFINED GRANT_CFG_INCLUDE_DIR)
  add_definitions(-DGRANTS_CFG)
endif()

IF (${ASSERT_NOT_CORE})
    ADD_DEFINITIONS(-DASSERT_NOT_CORE)
    MESSAGE(STATUS "disable assert core")
ELSE ()
    MESSAGE(STATUS "enable assert core")
ENDIF (${ASSERT_NOT_CORE})

target_include_directories(
    util
    PUBLIC "${TD_SOURCE_DIR}/include/util"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    PRIVATE "${TD_SOURCE_DIR}/include/common"
    PRIVATE "${GRANT_CFG_INCLUDE_DIR}"
)

if (${TD_CONTRIB_STATIC} AND NOT ${BUILD_CONTRIB})
    target_include_directories(
        util 
        PUBLIC "${TD_SOURCE_DIR}/deps/${TD_DEPS_DIR}/geos_static"
    )

    target_link_directories(
        util 
        PUBLIC "${TD_SOURCE_DIR}/deps/${TD_DEPS_DIR}/geos_static"
    )
    target_link_libraries(
        util
        PUBLIC os
        PUBLIC lz4_static
        PUBLIC api cjson geos_c geos
    )
else()
    target_link_libraries(
        util
        PUBLIC os
        PUBLIC lz4_static
        PUBLIC api cjson geos_c
    )
endif()

if(${BUILD_TEST})
    ADD_SUBDIRECTORY(test)
endif(${BUILD_TEST})
