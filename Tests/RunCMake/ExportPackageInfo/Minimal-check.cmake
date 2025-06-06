include(${CMAKE_CURRENT_LIST_DIR}/Assertions.cmake)

set(out_dir "${RunCMake_BINARY_DIR}/Minimal-build")

file(READ "${out_dir}/foo.cps" content)
expect_value("${content}" "foo" "name")
expect_value("${content}" "interface" "components" "foo" "type")
expect_missing("${content}" "version")
expect_missing("${content}" "configurations")
expect_missing("${content}" "default_targets")
expect_missing("${content}" "components" "foo" "compile_definitions")
expect_missing("${content}" "components" "foo" "compile_features")
expect_missing("${content}" "components" "foo" "compile_flags")
expect_missing("${content}" "components" "foo" "link_directories")
expect_missing("${content}" "components" "foo" "link_features")
expect_missing("${content}" "components" "foo" "link_flags")
expect_missing("${content}" "components" "foo" "link_libraries")
expect_missing("${content}" "components" "foo" "requires")
expect_missing("${content}" "components" "foo" "license")
