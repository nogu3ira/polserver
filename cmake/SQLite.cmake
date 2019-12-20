message("* sqlite")
set(SQLITE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/sqlite")
if (${linux})
  set(SQLITE_LIB "${SQLITE_SOURCE_DIR}/libsqlite.a")
else()
  set(SQLITE_LIB "${SQLITE_SOURCE_DIR}/Release/sqlite.lib")
endif()

if ( NOT EXISTS "${SQLITE_LIB}" )
  ExternalProject_Add(sqlite
    URL "${SQLITE_SOURCE_DIR}/../sqlite.zip" 
    SOURCE_DIR "${SQLITE_SOURCE_DIR}"
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_LIST_DIR}/SqliteList.txt" "${SQLITE_SOURCE_DIR}/CMakeLists.txt"
    PREFIX sqlite
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    BUILD_IN_SOURCE 1
    #no install step
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${SQLITE_LIB}
    LOG_DOWNLOAD 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
  )
endif()
