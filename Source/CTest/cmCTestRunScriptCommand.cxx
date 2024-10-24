/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmCTestRunScriptCommand.h"

#include "cmCTestScriptHandler.h"
#include "cmExecutionStatus.h"
#include "cmMakefile.h"
#include "cmSystemTools.h"

bool cmCTestRunScriptCommand::InitialPass(std::vector<std::string> const& args,
                                          cmExecutionStatus& status)
{
  if (args.empty()) {
    status.SetError("called with incorrect number of arguments");
    return false;
  }

  bool np = false;
  unsigned int i = 0;
  if (args[i] == "NEW_PROCESS") {
    np = true;
    i++;
  }
  int start = i;
  // run each script
  std::string returnVariable;
  for (i = start; i < args.size(); ++i) {
    if (args[i] == "RETURN_VALUE") {
      ++i;
      if (i < args.size()) {
        returnVariable = args[i];
      }
    }
  }
  for (i = start; i < args.size(); ++i) {
    if (args[i] == "RETURN_VALUE") {
      ++i;
    } else {
      int ret;
      cmCTestScriptHandler::RunScript(this->CTest, this->Makefile,
                                      cmSystemTools::CollapseFullPath(args[i]),
                                      !np, &ret);
      this->Makefile->AddDefinition(returnVariable, std::to_string(ret));
    }
  }
  return true;
}
