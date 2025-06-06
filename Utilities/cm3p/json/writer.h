/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#pragma once

/* Use the jsoncpp library configured for CMake.  */
#include "cmThirdParty.h"
#ifdef CMAKE_USE_SYSTEM_JSONCPP
#  include <json/writer.h> // IWYU pragma: export
#else
#  include <cmjsoncpp/include/json/writer.h> // IWYU pragma: export
#endif
