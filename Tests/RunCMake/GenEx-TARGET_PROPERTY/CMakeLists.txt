cmake_minimum_required(VERSION 3.17)
if(RunCMake_TEST STREQUAL "LOCATION")
  cmake_minimum_required(VERSION 2.8.12) # Leave CMP0026 unset.
endif()
project(${RunCMake_TEST} NONE)
include(${RunCMake_TEST}.cmake NO_POLICY_SCOPE)
