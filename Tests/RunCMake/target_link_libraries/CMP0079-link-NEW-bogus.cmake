cmake_policy(SET CMP0079 NEW)
enable_language(C)

add_executable(top empty.c)
set_property(TARGET top APPEND PROPERTY LINK_LIBRARIES "::@(0xdeadbeef);foo;::@")
