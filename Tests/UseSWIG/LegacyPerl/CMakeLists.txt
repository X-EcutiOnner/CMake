cmake_minimum_required(VERSION 3.10)

project(TestLegacyPerl CXX)

include(CTest)

if(NOT DEFINED language)
  set(language "perl")
endif()

include (../LegacyConfiguration.cmake)

add_test (NAME LegacyPerl
  COMMAND "${PERL_EXECUTABLE}" "-I${CMAKE_CURRENT_BINARY_DIR}"
                               "-I$<TARGET_FILE_DIR:${SWIG_MODULE_example_REAL_NAME}>"
  "${CMAKE_CURRENT_SOURCE_DIR}/../runme.pl")
