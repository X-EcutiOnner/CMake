/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <string>
#include <vector>

#include "cmFindBase.h"

class cmExecutionStatus;

/** \class cmFindPathCommand
 * \brief Define a command to search for a library.
 *
 * cmFindPathCommand is used to define a CMake variable
 * that specifies a library. The command searches for a given
 * file in a list of directories.
 */
class cmFindPathCommand : public cmFindBase
{
public:
  cmFindPathCommand(cmExecutionStatus& status);
  cmFindPathCommand(std::string findCommandName, cmExecutionStatus& status);

  bool InitialPass(std::vector<std::string> const& args);

  bool IncludeFileInPath;

private:
  std::string FindHeaderInFramework(std::string const& file,
                                    std::string const& dir) const;
  std::string FindHeader();
  std::string FindNormalHeader();
  std::string FindFrameworkHeader();
};

bool cmFindPath(std::vector<std::string> const& args,
                cmExecutionStatus& status);
