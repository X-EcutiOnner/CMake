/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#include "cmFindBase.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <iterator>
#include <map>
#include <memory>
#include <utility>

#include <cm/optional>
#include <cmext/algorithm>
#include <cmext/string_view>

#include "cmCMakePath.h"
#include "cmConfigureLog.h"
#include "cmExecutionStatus.h"
#include "cmList.h"
#include "cmListFileCache.h"
#include "cmMakefile.h"
#include "cmMessageType.h"
#include "cmPolicies.h"
#include "cmRange.h"
#include "cmSearchPath.h"
#include "cmState.h"
#include "cmStateTypes.h"
#include "cmStringAlgorithms.h"
#include "cmSystemTools.h"
#include "cmValue.h"
#include "cmWindowsRegistry.h"
#include "cmake.h"

cmFindBase::cmFindBase(std::string findCommandName, cmExecutionStatus& status)
  : cmFindCommon(status)
  , FindCommandName(std::move(findCommandName))
{
}

cmFindBase::~cmFindBase()
{
  if (this->DebugState) {
    this->DebugState->Write();
  }
}

bool cmFindBase::ParseArguments(std::vector<std::string> const& argsIn)
{
  if (argsIn.size() < 2) {
    this->SetError("called with incorrect number of arguments");
    return false;
  }

  // copy argsIn into args so it can be modified,
  // in the process extract the DOC "documentation"
  // and handle options NO_CACHE and ENV
  size_t size = argsIn.size();
  std::vector<std::string> args;
  bool foundDoc = false;
  for (unsigned int j = 0; j < size; ++j) {
    if (foundDoc || argsIn[j] != "DOC") {
      if (argsIn[j] == "NO_CACHE") {
        this->StoreResultInCache = false;
      } else if (argsIn[j] == "ENV") {
        if (j + 1 < size) {
          j++;
          std::vector<std::string> p =
            cmSystemTools::GetEnvPathNormalized(argsIn[j]);
          std::move(p.begin(), p.end(), std::back_inserter(args));
        }
      } else {
        args.push_back(argsIn[j]);
      }
    } else {
      if (j + 1 < size) {
        foundDoc = true;
        this->VariableDocumentation = argsIn[j + 1];
        j++;
        if (j >= size) {
          break;
        }
      }
    }
  }
  if (args.size() < 2) {
    this->SetError("called with incorrect number of arguments");
    return false;
  }
  this->VariableName = args[0];
  this->InitialState = this->GetInitialState();
  if (this->IsFound()) {
    if (this->DebugState) {
      this->DebugState->FoundAt(
        *this->Makefile->GetDefinition(this->VariableName));
    }
    return true;
  }

  // Find what search path locations have been enabled/disable
  this->SelectDefaultSearchModes();

  // Find the current root path mode.
  this->SelectDefaultRootPathMode();

  // Find the current bundle/framework search policy.
  this->SelectDefaultMacMode();

  bool newStyle = false;
  bool haveRequiredOrOptional = false;
  enum Doing
  {
    DoingNone,
    DoingNames,
    DoingPaths,
    DoingPathSuffixes,
    DoingHints
  };
  Doing doing = DoingNames; // assume it starts with a name
  for (unsigned int j = 1; j < args.size(); ++j) {
    if (args[j] == "NAMES") {
      doing = DoingNames;
      newStyle = true;
    } else if (args[j] == "PATHS") {
      doing = DoingPaths;
      newStyle = true;
    } else if (args[j] == "HINTS") {
      doing = DoingHints;
      newStyle = true;
    } else if (args[j] == "PATH_SUFFIXES") {
      doing = DoingPathSuffixes;
      newStyle = true;
    } else if (args[j] == "NAMES_PER_DIR") {
      doing = DoingNone;
      if (this->NamesPerDirAllowed) {
        this->NamesPerDir = true;
      } else {
        this->SetError("does not support NAMES_PER_DIR");
        return false;
      }
    } else if (args[j] == "NO_SYSTEM_PATH") {
      doing = DoingNone;
      this->NoDefaultPath = true;
    } else if (args[j] == "REQUIRED") {
      doing = DoingNone;
      if (haveRequiredOrOptional && !this->Required) {
        this->SetError("cannot be both REQUIRED and OPTIONAL");
        return false;
      }
      this->Required = true;
      newStyle = true;
      haveRequiredOrOptional = true;
    } else if (args[j] == "OPTIONAL") {
      doing = DoingNone;
      if (haveRequiredOrOptional && this->Required) {
        this->SetError("cannot be both REQUIRED and OPTIONAL");
        return false;
      }
      newStyle = true;
      haveRequiredOrOptional = true;
    } else if (args[j] == "REGISTRY_VIEW") {
      if (++j == args.size()) {
        this->SetError("missing required argument for REGISTRY_VIEW");
        return false;
      }
      auto view = cmWindowsRegistry::ToView(args[j]);
      if (view) {
        this->RegistryView = *view;
      } else {
        this->SetError(
          cmStrCat("given invalid value for REGISTRY_VIEW: ", args[j]));
        return false;
      }
    } else if (args[j] == "VALIDATOR") {
      if (++j == args.size()) {
        this->SetError("missing required argument for VALIDATOR");
        return false;
      }
      auto command = this->Makefile->GetState()->GetCommand(args[j]);
      if (!command) {
        this->SetError(cmStrCat(
          "command specified for VALIDATOR is undefined: ", args[j], '.'));
        return false;
      }
      // ensure a macro is not specified as validator
      auto const& validatorName = args[j];
      cmList macros{ this->Makefile->GetProperty("MACROS") };
      if (std::find_if(macros.begin(), macros.end(),
                       [&validatorName](std::string const& item) {
                         return cmSystemTools::Strucmp(validatorName.c_str(),
                                                       item.c_str()) == 0;
                       }) != macros.end()) {
        this->SetError(cmStrCat(
          "command specified for VALIDATOR is not a function: ", args[j],
          '.'));
        return false;
      }
      this->ValidatorName = args[j];
    } else if (this->CheckCommonArgument(args[j])) {
      doing = DoingNone;
    } else {
      // Some common arguments were accidentally supported by CMake
      // 2.4 and 2.6.0 in the short-hand form of the command, so we
      // must support it even though it is not documented.
      if (doing == DoingNames) {
        this->Names.push_back(args[j]);
      } else if (doing == DoingPaths) {
        this->UserGuessArgs.push_back(args[j]);
      } else if (doing == DoingHints) {
        this->UserHintsArgs.push_back(args[j]);
      } else if (doing == DoingPathSuffixes) {
        this->AddPathSuffix(args[j]);
      }
    }
  }

  if (!haveRequiredOrOptional) {
    this->Required = this->Makefile->IsOn("CMAKE_FIND_REQUIRED");
  }

  if (this->VariableDocumentation.empty()) {
    this->VariableDocumentation = "Where can ";
    if (this->Names.empty()) {
      this->VariableDocumentation += "the (unknown) library be found";
    } else if (this->Names.size() == 1) {
      this->VariableDocumentation +=
        "the " + this->Names.front() + " library be found";
    } else {
      this->VariableDocumentation += "one of the ";
      this->VariableDocumentation +=
        cmJoin(cmMakeRange(this->Names).retreat(1), ", ");
      this->VariableDocumentation +=
        " or " + this->Names.back() + " libraries be found";
    }
  }

  // look for old style
  // FIND_*(VAR name path1 path2 ...)
  if (!newStyle && !this->Names.empty()) {
    // All the short-hand arguments have been recorded as names.
    std::vector<std::string> shortArgs = this->Names;
    this->Names.clear(); // clear out any values in Names
    this->Names.push_back(shortArgs[0]);
    cm::append(this->UserGuessArgs, shortArgs.begin() + 1, shortArgs.end());
  }
  this->ExpandPaths();

  this->ComputeFinalPaths(IgnorePaths::Yes);

  return true;
}

bool cmFindBase::Validate(std::string const& path) const
{
  if (this->ValidatorName.empty()) {
    return true;
  }

  // The validator command will be executed in an isolated scope.
  cmMakefile::ScopePushPop varScope(this->Makefile);
  cmMakefile::PolicyPushPop polScope(this->Makefile);
  static_cast<void>(varScope);
  static_cast<void>(polScope);

  auto resultName =
    cmStrCat("CMAKE_"_s, cmSystemTools::UpperCase(this->FindCommandName),
             "_VALIDATOR_STATUS"_s);

  this->Makefile->AddDefinitionBool(resultName, true);

  cmListFileFunction validator(
    this->ValidatorName, 0, 0,
    { cmListFileArgument(resultName, cmListFileArgument::Unquoted, 0),
      cmListFileArgument(path, cmListFileArgument::Quoted, 0) });
  cmExecutionStatus status(*this->Makefile);

  if (this->Makefile->ExecuteCommand(validator, status)) {
    return this->Makefile->GetDefinition(resultName).IsOn();
  }
  return false;
}

void cmFindBase::ExpandPaths()
{
  if (!this->NoDefaultPath) {
    if (!this->NoPackageRootPath) {
      this->FillPackageRootPath();
    }
    if (!this->NoCMakePath) {
      this->FillCMakeVariablePath();
    }
    if (!this->NoCMakeEnvironmentPath) {
      this->FillCMakeEnvironmentPath();
    }
  }
  this->FillUserHintsPath();
  if (!this->NoDefaultPath) {
    if (!this->NoSystemEnvironmentPath) {
      this->FillSystemEnvironmentPath();
    }
    if (!this->NoCMakeSystemPath) {
      this->FillCMakeSystemVariablePath();
    }
  }
  this->FillUserGuessPath();
}

void cmFindBase::FillCMakeEnvironmentPath()
{
  cmSearchPath& paths = this->LabeledPaths[PathLabel::CMakeEnvironment];

  // Add CMAKE_*_PATH environment variables
  std::string var = cmStrCat("CMAKE_", this->CMakePathName, "_PATH");
  paths.AddEnvPrefixPath("CMAKE_PREFIX_PATH");
  paths.AddEnvPath(var);

  if (this->CMakePathName == "PROGRAM") {
    paths.AddEnvPath("CMAKE_APPBUNDLE_PATH");
  } else {
    paths.AddEnvPath("CMAKE_FRAMEWORK_PATH");
  }
  paths.AddSuffixes(this->SearchPathSuffixes);
}

void cmFindBase::FillPackageRootPath()
{
  cmSearchPath& paths = this->LabeledPaths[PathLabel::PackageRoot];

  // Add the PACKAGE_ROOT_PATH from each enclosing find_package call.
  for (std::vector<std::string> const& pkgPaths :
       cmReverseRange(this->Makefile->FindPackageRootPathStack)) {
    paths.AddPrefixPaths(pkgPaths);
  }

  paths.AddSuffixes(this->SearchPathSuffixes);
}

void cmFindBase::FillCMakeVariablePath()
{
  cmSearchPath& paths = this->LabeledPaths[PathLabel::CMake];

  // Add CMake variables of the same name as the previous environment
  // variables CMAKE_*_PATH to be used most of the time with -D
  // command line options
  std::string var = cmStrCat("CMAKE_", this->CMakePathName, "_PATH");
  paths.AddCMakePrefixPath("CMAKE_PREFIX_PATH");
  paths.AddCMakePath(var);

  if (this->CMakePathName == "PROGRAM") {
    paths.AddCMakePath("CMAKE_APPBUNDLE_PATH");
  } else {
    paths.AddCMakePath("CMAKE_FRAMEWORK_PATH");
  }
  paths.AddSuffixes(this->SearchPathSuffixes);
}

void cmFindBase::FillSystemEnvironmentPath()
{
  cmSearchPath& paths = this->LabeledPaths[PathLabel::SystemEnvironment];

  // Add LIB or INCLUDE
  if (!this->EnvironmentPath.empty()) {
    paths.AddEnvPath(this->EnvironmentPath);
  }
  // Add PATH
  paths.AddEnvPath("PATH");
  paths.AddSuffixes(this->SearchPathSuffixes);
}

namespace {
struct entry_to_remove
{
  entry_to_remove(std::string const& name, cmMakefile* makefile)
  {
    if (cmValue to_skip = makefile->GetDefinition(
          cmStrCat("_CMAKE_SYSTEM_PREFIX_PATH_", name, "_PREFIX_COUNT"))) {
      cmStrToLong(*to_skip, &count);
    }
    if (cmValue prefix_value = makefile->GetDefinition(
          cmStrCat("_CMAKE_SYSTEM_PREFIX_PATH_", name, "_PREFIX_VALUE"))) {
      value = *prefix_value;
    }
  }
  bool valid() const { return count > 0 && !value.empty(); }

  void remove_self(std::vector<std::string>& entries) const
  {
    if (this->valid()) {
      long to_skip = this->count;
      size_t index_to_remove = 0;
      for (auto const& path : entries) {
        if (path == this->value && --to_skip == 0) {
          break;
        }
        ++index_to_remove;
      }
      if (index_to_remove < entries.size() && to_skip == 0) {
        entries.erase(entries.begin() + index_to_remove);
      }
    }
  }

  long count = -1;
  std::string value;
};
}

void cmFindBase::FillCMakeSystemVariablePath()
{
  cmSearchPath& paths = this->LabeledPaths[PathLabel::CMakeSystem];

  bool const install_prefix_in_list =
    !this->Makefile->IsOn("CMAKE_FIND_NO_INSTALL_PREFIX");
  bool const remove_install_prefix = this->NoCMakeInstallPath;
  bool const add_install_prefix = !this->NoCMakeInstallPath &&
    this->Makefile->IsDefinitionSet("CMAKE_FIND_USE_INSTALL_PREFIX");

  // We have 3 possible states for `CMAKE_SYSTEM_PREFIX_PATH` and
  // `CMAKE_INSTALL_PREFIX`.
  // Either we need to remove `CMAKE_INSTALL_PREFIX`, add
  // `CMAKE_INSTALL_PREFIX`, or do nothing.
  //
  // When we need to remove `CMAKE_INSTALL_PREFIX` we remove the Nth occurrence
  // of `CMAKE_INSTALL_PREFIX` from `CMAKE_SYSTEM_PREFIX_PATH`, where `N` is
  // computed by `CMakeSystemSpecificInformation.cmake` while constructing
  // `CMAKE_SYSTEM_PREFIX_PATH`. This ensures that if projects / toolchains
  // have removed `CMAKE_INSTALL_PREFIX` from the list, we don't remove
  // some other entry by mistake ( likewise for `CMAKE_STAGING_PREFIX` )
  entry_to_remove install_entry("INSTALL", this->Makefile);
  entry_to_remove staging_entry("STAGING", this->Makefile);

  if (remove_install_prefix && install_prefix_in_list &&
      (install_entry.valid() || staging_entry.valid())) {
    cmValue prefix_paths =
      this->Makefile->GetDefinition("CMAKE_SYSTEM_PREFIX_PATH");

    // remove entries from CMAKE_SYSTEM_PREFIX_PATH
    cmList expanded{ *prefix_paths };
    install_entry.remove_self(expanded);
    staging_entry.remove_self(expanded);
    for (std::string& p : expanded) {
      p = cmSystemTools::CollapseFullPath(
        p, this->Makefile->GetCurrentSourceDirectory());
    }
    paths.AddPrefixPaths(expanded);
  } else if (add_install_prefix && !install_prefix_in_list) {
    paths.AddCMakePrefixPath("CMAKE_INSTALL_PREFIX");
    paths.AddCMakePrefixPath("CMAKE_STAGING_PREFIX");
    paths.AddCMakePrefixPath("CMAKE_SYSTEM_PREFIX_PATH");
  } else {
    // Otherwise the current setup of `CMAKE_SYSTEM_PREFIX_PATH` is correct
    paths.AddCMakePrefixPath("CMAKE_SYSTEM_PREFIX_PATH");
  }

  std::string var = cmStrCat("CMAKE_SYSTEM_", this->CMakePathName, "_PATH");
  paths.AddCMakePath(var);

  if (this->CMakePathName == "PROGRAM") {
    paths.AddCMakePath("CMAKE_SYSTEM_APPBUNDLE_PATH");
  } else {
    paths.AddCMakePath("CMAKE_SYSTEM_FRAMEWORK_PATH");
  }
  paths.AddSuffixes(this->SearchPathSuffixes);
}

void cmFindBase::FillUserHintsPath()
{
  cmSearchPath& paths = this->LabeledPaths[PathLabel::Hints];

  for (std::string const& p : this->UserHintsArgs) {
    paths.AddUserPath(p);
  }
  paths.AddSuffixes(this->SearchPathSuffixes);
}

void cmFindBase::FillUserGuessPath()
{
  cmSearchPath& paths = this->LabeledPaths[PathLabel::Guess];

  for (std::string const& p : this->UserGuessArgs) {
    paths.AddUserPath(p);
  }
  paths.AddSuffixes(this->SearchPathSuffixes);
}

cmFindBase::FindState cmFindBase::GetInitialState()
{
  if (cmValue value = this->Makefile->GetDefinition(this->VariableName)) {
    cmState* state = this->Makefile->GetState();
    cmValue cacheEntry = state->GetCacheEntryValue(this->VariableName);
    bool found = !cmIsNOTFOUND(*value);
    bool cached = cacheEntry != nullptr;
    auto cacheType = cached ? state->GetCacheEntryType(this->VariableName)
                            : cmStateEnums::UNINITIALIZED;

    if (cached && cacheType != cmStateEnums::UNINITIALIZED) {
      this->VariableType = cacheType;
      if (auto const& hs =
            state->GetCacheEntryProperty(this->VariableName, "HELPSTRING")) {
        this->VariableDocumentation = *hs;
      }
    }

    if (found) {
      // If the user specifies the entry on the command line without a
      // type we should add the type and docstring but keep the
      // original value.  Tell the subclass implementations to do
      // this.
      if (cached && cacheType == cmStateEnums::UNINITIALIZED) {
        this->AlreadyInCacheWithoutMetaInfo = true;
      }
      return FindState::Found;
    }
    return FindState::NotFound;
  }
  return FindState::Undefined;
}

bool cmFindBase::IsFound() const
{
  return this->InitialState == FindState::Found;
}

bool cmFindBase::IsDefined() const
{
  return this->InitialState != FindState::Undefined;
}

void cmFindBase::NormalizeFindResult()
{
  std::string foundValue;
  if (this->Makefile->GetPolicyStatus(cmPolicies::CMP0125) ==
      cmPolicies::NEW) {
    // ensure the path returned by find_* command is absolute
    auto const& existingValue =
      this->Makefile->GetDefinition(this->VariableName);
    std::string value;
    if (!existingValue->empty()) {
      value =
        cmCMakePath(*existingValue, cmCMakePath::auto_format)
          .Absolute(cmCMakePath(
            this->Makefile->GetCMakeInstance()->GetCMakeWorkingDirectory()))
          .Normal()
          .GenericString();
      if (!cmSystemTools::FileExists(value, false)) {
        value = *existingValue;
      }
    }

    foundValue = value;
    if (this->StoreResultInCache) {
      // If the user specifies the entry on the command line without a
      // type we should add the type and docstring but keep the original
      // value.
      if (value != *existingValue || this->AlreadyInCacheWithoutMetaInfo) {
        this->Makefile->GetCMakeInstance()->AddCacheEntry(
          this->VariableName, value, this->VariableDocumentation,
          this->VariableType);
        if (this->Makefile->GetPolicyStatus(cmPolicies::CMP0126) ==
            cmPolicies::NEW) {
          if (this->Makefile->IsNormalDefinitionSet(this->VariableName)) {
            this->Makefile->AddDefinition(this->VariableName, value);
          }
        } else {
          // if there was a definition then remove it
          // This is required to ensure same behavior as
          // cmMakefile::AddCacheDefinition.
          this->Makefile->RemoveDefinition(this->VariableName);
        }
      }
    } else {
      // ensure a normal variable is defined.
      this->Makefile->AddDefinition(this->VariableName, value);
    }
  } else {
    // If the user specifies the entry on the command line without a
    // type we should add the type and docstring but keep the original
    // value.
    if (this->StoreResultInCache) {
      auto const& existingValue =
        this->Makefile->GetCMakeInstance()->GetCacheDefinition(
          this->VariableName);
      foundValue = *existingValue;
      if (this->AlreadyInCacheWithoutMetaInfo) {
        this->Makefile->AddCacheDefinition(this->VariableName, "",
                                           this->VariableDocumentation,
                                           this->VariableType);
        if (this->Makefile->GetPolicyStatus(cmPolicies::CMP0126) ==
              cmPolicies::NEW &&
            this->Makefile->IsNormalDefinitionSet(this->VariableName)) {
          this->Makefile->AddDefinition(this->VariableName, *existingValue);
        }
      }
    } else {
      auto const& existingValue =
        this->Makefile->GetSafeDefinition(this->VariableName);
      foundValue = existingValue;
      // ensure a normal variable is defined.
      this->Makefile->AddDefinition(this->VariableName, existingValue);
    }
  }
  if (this->DebugState) {
    this->DebugState->FoundAt(foundValue);
  }
}

void cmFindBase::StoreFindResult(std::string const& value)
{
  bool force =
    this->Makefile->GetPolicyStatus(cmPolicies::CMP0125) == cmPolicies::NEW;
  bool updateNormalVariable =
    this->Makefile->GetPolicyStatus(cmPolicies::CMP0126) == cmPolicies::NEW;

  if (!value.empty()) {
    if (this->StoreResultInCache) {
      this->Makefile->AddCacheDefinition(this->VariableName, value,
                                         this->VariableDocumentation,
                                         this->VariableType, force);
      if (updateNormalVariable &&
          this->Makefile->IsNormalDefinitionSet(this->VariableName)) {
        this->Makefile->AddDefinition(this->VariableName, value);
      }
    } else {
      this->Makefile->AddDefinition(this->VariableName, value);
    }

    if (this->DebugState) {
      this->DebugState->FoundAt(value);
    }

    return;
  }

  auto notFound = cmStrCat(this->VariableName, "-NOTFOUND");
  if (this->StoreResultInCache) {
    this->Makefile->AddCacheDefinition(this->VariableName, notFound,
                                       this->VariableDocumentation,
                                       this->VariableType, force);
    if (updateNormalVariable &&
        this->Makefile->IsNormalDefinitionSet(this->VariableName)) {
      this->Makefile->AddDefinition(this->VariableName, notFound);
    }
  } else {
    this->Makefile->AddDefinition(this->VariableName, notFound);
  }

  if (this->Required) {
    this->Makefile->IssueMessage(
      MessageType::FATAL_ERROR,
      cmStrCat("Could not find ", this->VariableName, " using the following ",
               (this->FindCommandName == "find_file" ||
                    this->FindCommandName == "find_path"
                  ? "files"
                  : "names"),
               ": ", cmJoin(this->Names, ", ")));
    cmSystemTools::SetFatalErrorOccurred();
  }
}

cmFindBaseDebugState::cmFindBaseDebugState(cmFindBase const* findBase)
  : cmFindCommonDebugState(findBase->FindCommandName, findBase)
  , FindBaseCommand(findBase)
{
}

cmFindBaseDebugState::~cmFindBaseDebugState() = default;

void cmFindBaseDebugState::FoundAtImpl(std::string const& path,
                                       std::string regexName)
{
  this->FoundSearchLocation = DebugLibState{ std::move(regexName), path };
}

void cmFindBaseDebugState::FailedAtImpl(std::string const& path,
                                        std::string regexName)
{
  this->FailedSearchLocations.emplace_back(std::move(regexName), path);
}

void cmFindBaseDebugState::WriteDebug() const
{
  // clang-format off
  auto buffer =
    cmStrCat(
      this->CommandName, " called with the following settings:"
      "\n  VAR: ", this->FindBaseCommand->VariableName,
      "\n  NAMES: ", cmWrap('"', this->FindBaseCommand->Names, '"', "\n         "),
      "\n  Documentation: ", this->FindBaseCommand->VariableDocumentation,
      "\n  Framework"
      "\n    Only Search Frameworks: ", this->FindBaseCommand->SearchFrameworkOnly,
      "\n    Search Frameworks Last: ", this->FindBaseCommand->SearchFrameworkLast,
      "\n    Search Frameworks First: ", this->FindBaseCommand->SearchFrameworkFirst,
      "\n  AppBundle"
      "\n    Only Search AppBundle: ", this->FindBaseCommand->SearchAppBundleOnly,
      "\n    Search AppBundle Last: ", this->FindBaseCommand->SearchAppBundleLast,
      "\n    Search AppBundle First: ", this->FindBaseCommand->SearchAppBundleFirst,
      '\n'
    );
  // clang-format on

  if (this->FindCommand->NoDefaultPath) {
    buffer += "  NO_DEFAULT_PATH Enabled\n";
  } else {
    // clang-format off
    buffer += cmStrCat(
      "  CMAKE_FIND_USE_CMAKE_PATH: ", !this->FindCommand->NoCMakePath,
      "\n  CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH: ", !this->FindCommand->NoCMakeEnvironmentPath,
      "\n  CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH: ", !this->FindCommand->NoSystemEnvironmentPath,
      "\n  CMAKE_FIND_USE_CMAKE_SYSTEM_PATH: ", !this->FindCommand->NoCMakeSystemPath,
      "\n  CMAKE_FIND_USE_INSTALL_PREFIX: ", !this->FindCommand->NoCMakeInstallPath,
      '\n'
     );
    // clang-format on
  }

  buffer +=
    cmStrCat(this->CommandName, " considered the following locations:\n");
  for (auto const& state : this->FailedSearchLocations) {
    std::string path = cmStrCat("  ", state.path);
    if (!state.regexName.empty()) {
      path = cmStrCat(path, '/', state.regexName);
    }
    buffer += cmStrCat(path, '\n');
  }

  if (this->HasBeenFound()) {
    buffer += cmStrCat("The item was found at\n  ",
                       this->FoundSearchLocation.path, '\n');
  } else {
    buffer += "The item was not found.\n";
  }

  this->FindCommand->DebugMessage(buffer);
}

#ifndef CMAKE_BOOTSTRAP
void cmFindBaseDebugState::WriteEvent(cmConfigureLog& log,
                                      cmMakefile const& mf) const
{
  log.BeginEvent("find-v1", mf);

  // Mode is the Command name without the "find_" prefix
  log.WriteValue("mode"_s, this->CommandName.substr(5));
  log.WriteValue("variable"_s, this->FindBaseCommand->VariableName);
  log.WriteValue("description"_s,
                 this->FindBaseCommand->VariableDocumentation);

  // Yes, this needs to return a `std::string`. If it returns a `const char*`,
  // the `WriteValue` method prefers the `bool` overload. There's no overload
  // for a `cm::string_view` because the underlying JSON library doesn't
  // support `string_view` arguments itself.
  auto search_opt_to_str = [](bool first, bool last,
                              bool only) -> std::string {
    return first ? "FIRST" : (last ? "LAST" : (only ? "ONLY" : "NEVER"));
  };
  log.BeginObject("settings"_s);
  log.WriteValue("SearchFramework"_s,
                 search_opt_to_str(this->FindCommand->SearchFrameworkFirst,
                                   this->FindCommand->SearchFrameworkLast,
                                   this->FindCommand->SearchFrameworkOnly));
  log.WriteValue("SearchAppBundle"_s,
                 search_opt_to_str(this->FindCommand->SearchAppBundleFirst,
                                   this->FindCommand->SearchAppBundleLast,
                                   this->FindCommand->SearchAppBundleOnly));
  log.WriteValue("CMAKE_FIND_USE_CMAKE_PATH"_s,
                 !this->FindCommand->NoCMakePath);
  log.WriteValue("CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH"_s,
                 !this->FindCommand->NoCMakeEnvironmentPath);
  log.WriteValue("CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH"_s,
                 !this->FindCommand->NoSystemEnvironmentPath);
  log.WriteValue("CMAKE_FIND_USE_CMAKE_SYSTEM_PATH"_s,
                 !this->FindCommand->NoCMakeSystemPath);
  log.WriteValue("CMAKE_FIND_USE_INSTALL_PREFIX"_s,
                 !this->FindCommand->NoCMakeInstallPath);
  log.EndObject();

  if (!this->FindBaseCommand->Names.empty()) {
    log.WriteValue("names"_s, this->FindBaseCommand->Names);
  }
  std::vector<std::string> directories;
  directories.reserve(this->FailedSearchLocations.size());
  for (auto const& location : this->FailedSearchLocations) {
    directories.push_back(location.path);
  }
  if (!this->FindCommand->SearchPaths.empty()) {
    log.WriteValue("candidate_directories"_s, this->FindCommand->SearchPaths);
  }
  if (!directories.empty()) {
    log.WriteValue("searched_directories"_s, directories);
  }
  if (!this->FoundSearchLocation.path.empty()) {
    log.WriteValue("found"_s, this->FoundSearchLocation.path);
  } else {
    log.WriteValue("found"_s, false);
  }

  this->WriteSearchVariables(log, mf);

  log.EndEvent();
}

std::vector<std::pair<cmFindCommonDebugState::VariableSource, std::string>>
cmFindBaseDebugState::ExtraSearchVariables() const
{
  std::vector<std::pair<cmFindCommonDebugState::VariableSource, std::string>>
    extraSearches;
  if (!this->FindBaseCommand->EnvironmentPath.empty()) {
    extraSearches.emplace_back(VariableSource::EnvironmentList,
                               this->FindBaseCommand->EnvironmentPath);
  }
  return extraSearches;
}
#endif
