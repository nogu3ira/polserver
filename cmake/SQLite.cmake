set(lib_name sqlite)

set(sqlite_sources 
  ${CMAKE_CURRENT_LIST_DIR}/../lib/sqlite-amalgamation-3290000/shell.c
  ${CMAKE_CURRENT_LIST_DIR}/../lib/sqlite-amalgamation-3290000/sqlite3.c
  ${CMAKE_CURRENT_LIST_DIR}/../lib/sqlite-amalgamation-3290000/sqlite3.h
  ${CMAKE_CURRENT_LIST_DIR}/../lib/sqlite-amalgamation-3290000/sqlite3ext.h
)

add_library(${lib_name} STATIC
  ${${lib_name}_sources}
)

set_target_properties (${lib_name} PROPERTIES FOLDER 3rdParty)
