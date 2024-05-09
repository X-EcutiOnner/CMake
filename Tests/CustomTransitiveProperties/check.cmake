set(expect [[
# file\(GENERATE\) produced:
iface1 CUSTOM_A: ''
iface1 INTERFACE_CUSTOM_A: 'CUSTOM_A_IFACE1;CUSTOM_A_TARGET_NAME_IFACE1'
iface2 CUSTOM_A: ''
iface2 INTERFACE_CUSTOM_A: 'CUSTOM_A_IFACE2;CUSTOM_A_TARGET_TYPE_INTERFACE_LIBRARY;CUSTOM_A_IFACE1;CUSTOM_A_TARGET_NAME_IFACE2'
static1 CUSTOM_A: 'CUSTOM_A_STATIC1;CUSTOM_A_IFACE2;CUSTOM_A_TARGET_TYPE_STATIC_LIBRARY;CUSTOM_A_IFACE1;CUSTOM_A_TARGET_NAME_STATIC1'
static1 INTERFACE_CUSTOM_A: 'CUSTOM_A_STATIC1_IFACE'
static1 CUSTOM_B: 'CUSTOM_B_STATIC1;CUSTOM_B_IFACE1'
static1 INTERFACE_CUSTOM_B: 'CUSTOM_B_STATIC1_IFACE'
object1 CUSTOM_A: 'CUSTOM_A_OBJECT1;CUSTOM_A_IFACE2;CUSTOM_A_TARGET_TYPE_OBJECT_LIBRARY;CUSTOM_A_IFACE1;CUSTOM_A_TARGET_NAME_OBJECT1'
object1 INTERFACE_CUSTOM_A: 'CUSTOM_A_OBJECT1_IFACE'
object1 CUSTOM_C: 'CUSTOM_C_OBJECT1;CUSTOM_C_IFACE1'
object1 INTERFACE_CUSTOM_C: 'CUSTOM_C_OBJECT1_IFACE'
main CUSTOM_A: 'CUSTOM_A_MAIN'
main INTERFACE_CUSTOM_A: ''
main CUSTOM_B: 'CUSTOM_B_MAIN;CUSTOM_B_STATIC1_IFACE'
main INTERFACE_CUSTOM_B: ''
main CUSTOM_C: 'CUSTOM_C_MAIN;CUSTOM_C_OBJECT1_IFACE'
main INTERFACE_CUSTOM_C: ''
]])
string(REGEX REPLACE "\r\n" "\n" expect "${expect}")
string(REGEX REPLACE "\n+$" "" expect "${expect}")

file(READ "${out}" actual)
string(REGEX REPLACE "\r\n" "\n" actual "${actual}")
string(REGEX REPLACE "\n+$" "" actual "${actual}")

if(NOT actual MATCHES "^${expect}$")
  string(REPLACE "\n" "\n expect> " expect " expect> ${expect}")
  string(REPLACE "\n" "\n actual> " actual " actual> ${actual}")
  message(FATAL_ERROR "Expected file(GENERATE) output:\n${expect}\ndoes not match actual output:\n${actual}")
endif()
