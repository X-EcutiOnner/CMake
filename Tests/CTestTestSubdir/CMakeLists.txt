cmake_minimum_required(VERSION 3.10)
project(CTestTestSubdir)
include(CTest)

add_subdirectory(subdir)
subdirs(subdir2)
subdirs("${CTestTestSubdir_SOURCE_DIR}/subdir3")
