file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/file" "")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/file/directory" RESULT resultVal)
message(STATUS "Result=${resultVal}")
