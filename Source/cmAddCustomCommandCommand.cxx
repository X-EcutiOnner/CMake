/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#include "cmAddCustomCommandCommand.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <unordered_set>
#include <utility>

#include <cm/memory>
#include <cmext/string_view>

#include "cmCustomCommand.h"
#include "cmCustomCommandLines.h"
#include "cmCustomCommandTypes.h"
#include "cmExecutionStatus.h"
#include "cmGeneratorExpression.h"
#include "cmGlobalGenerator.h"
#include "cmMakefile.h"
#include "cmMessageType.h"
#include "cmPolicies.h"
#include "cmStringAlgorithms.h"
#include "cmSystemTools.h"
#include "cmValue.h"

bool cmAddCustomCommandCommand(std::vector<std::string> const& args,
                               cmExecutionStatus& status)
{
  /* Let's complain at the end of this function about the lack of a particular
     arg. For the moment, let's say that COMMAND, and either TARGET or SOURCE
     are required.
  */
  if (args.size() < 4) {
    status.SetError("called with wrong number of arguments.");
    return false;
  }

  cmMakefile& mf = status.GetMakefile();
  std::string source;
  std::string target;
  std::string main_dependency;
  std::string working;
  std::string depfile;
  std::string job_pool;
  std::string job_server_aware;
  std::string comment_buffer;
  char const* comment = nullptr;
  std::vector<std::string> depends;
  std::vector<std::string> outputs;
  std::vector<std::string> output;
  std::vector<std::string> byproducts;
  bool verbatim = false;
  bool append = false;
  bool uses_terminal = false;
  bool command_expand_lists = false;
  bool depends_explicit_only =
    mf.IsOn("CMAKE_ADD_CUSTOM_COMMAND_DEPENDS_EXPLICIT_ONLY");
  bool codegen = false;
  std::string implicit_depends_lang;
  cmImplicitDependsList implicit_depends;

  // Accumulate one command line at a time.
  cmCustomCommandLine currentLine;

  // Save all command lines.
  cmCustomCommandLines commandLines;

  cmCustomCommandType cctype = cmCustomCommandType::POST_BUILD;

  enum tdoing
  {
    doing_source,
    doing_command,
    doing_target,
    doing_depends,
    doing_implicit_depends_lang,
    doing_implicit_depends_file,
    doing_main_dependency,
    doing_output,
    doing_outputs,
    doing_byproducts,
    doing_comment,
    doing_working_directory,
    doing_depfile,
    doing_job_pool,
    doing_job_server_aware,
    doing_nothing
  };

  tdoing doing = doing_nothing;

#define MAKE_STATIC_KEYWORD(KEYWORD)                                          \
  static const std::string key##KEYWORD = #KEYWORD
  MAKE_STATIC_KEYWORD(APPEND);
  MAKE_STATIC_KEYWORD(ARGS);
  MAKE_STATIC_KEYWORD(BYPRODUCTS);
  MAKE_STATIC_KEYWORD(COMMAND);
  MAKE_STATIC_KEYWORD(COMMAND_EXPAND_LISTS);
  MAKE_STATIC_KEYWORD(COMMENT);
  MAKE_STATIC_KEYWORD(DEPENDS);
  MAKE_STATIC_KEYWORD(DEPFILE);
  MAKE_STATIC_KEYWORD(IMPLICIT_DEPENDS);
  MAKE_STATIC_KEYWORD(JOB_POOL);
  MAKE_STATIC_KEYWORD(JOB_SERVER_AWARE);
  MAKE_STATIC_KEYWORD(MAIN_DEPENDENCY);
  MAKE_STATIC_KEYWORD(OUTPUT);
  MAKE_STATIC_KEYWORD(OUTPUTS);
  MAKE_STATIC_KEYWORD(POST_BUILD);
  MAKE_STATIC_KEYWORD(PRE_BUILD);
  MAKE_STATIC_KEYWORD(PRE_LINK);
  MAKE_STATIC_KEYWORD(SOURCE);
  MAKE_STATIC_KEYWORD(TARGET);
  MAKE_STATIC_KEYWORD(USES_TERMINAL);
  MAKE_STATIC_KEYWORD(VERBATIM);
  MAKE_STATIC_KEYWORD(WORKING_DIRECTORY);
  MAKE_STATIC_KEYWORD(DEPENDS_EXPLICIT_ONLY);
  MAKE_STATIC_KEYWORD(CODEGEN);
#undef MAKE_STATIC_KEYWORD
  static std::unordered_set<std::string> const keywords{
    keyAPPEND,
    keyARGS,
    keyBYPRODUCTS,
    keyCOMMAND,
    keyCOMMAND_EXPAND_LISTS,
    keyCOMMENT,
    keyDEPENDS,
    keyDEPFILE,
    keyIMPLICIT_DEPENDS,
    keyJOB_POOL,
    keyMAIN_DEPENDENCY,
    keyOUTPUT,
    keyOUTPUTS,
    keyPOST_BUILD,
    keyPRE_BUILD,
    keyPRE_LINK,
    keySOURCE,
    keyJOB_SERVER_AWARE,
    keyTARGET,
    keyUSES_TERMINAL,
    keyVERBATIM,
    keyWORKING_DIRECTORY,
    keyDEPENDS_EXPLICIT_ONLY,
    keyCODEGEN
  };
  /* clang-format off */
  static std::set<std::string> const supportedTargetKeywords{
    keyARGS,
    keyBYPRODUCTS,
    keyCOMMAND,
    keyCOMMAND_EXPAND_LISTS,
    keyCOMMENT,
    keyPOST_BUILD,
    keyPRE_BUILD,
    keyPRE_LINK,
    keyTARGET,
    keyUSES_TERMINAL,
    keyVERBATIM,
    keyWORKING_DIRECTORY
  };
  /* clang-format on */
  static std::set<std::string> const supportedOutputKeywords{
    keyAPPEND,
    keyARGS,
    keyBYPRODUCTS,
    keyCODEGEN,
    keyCOMMAND,
    keyCOMMAND_EXPAND_LISTS,
    keyCOMMENT,
    keyDEPENDS,
    keyDEPENDS_EXPLICIT_ONLY,
    keyDEPFILE,
    keyIMPLICIT_DEPENDS,
    keyJOB_POOL,
    keyJOB_SERVER_AWARE,
    keyMAIN_DEPENDENCY,
    keyOUTPUT,
    keyUSES_TERMINAL,
    keyVERBATIM,
    keyWORKING_DIRECTORY
  };
  /* clang-format off */
  static std::set<std::string> const supportedAppendKeywords{
    keyAPPEND,
    keyARGS,
    keyCOMMAND,
    keyCOMMENT,           // Allowed but ignored
    keyDEPENDS,
    keyIMPLICIT_DEPENDS,
    keyMAIN_DEPENDENCY,   // Allowed but ignored
    keyOUTPUT,
    keyWORKING_DIRECTORY  // Allowed but ignored
  };
  /* clang-format on */
  std::set<std::string> keywordsSeen;
  std::string const* keywordExpectingValue = nullptr;
  auto const cmp0175 = mf.GetPolicyStatus(cmPolicies::CMP0175);

  for (std::string const& copy : args) {
    if (keywords.count(copy)) {
      // Check if a preceding keyword expected a value but there wasn't one
      if (keywordExpectingValue) {
        std::string const msg =
          cmStrCat("Keyword ", *keywordExpectingValue,
                   " requires a value, but none was given.");
        if (cmp0175 == cmPolicies::NEW) {
          mf.IssueMessage(MessageType::FATAL_ERROR, msg);
          return false;
        }
        if (cmp0175 == cmPolicies::WARN) {
          mf.IssueMessage(
            MessageType::AUTHOR_WARNING,
            cmStrCat(msg, '\n',
                     cmPolicies::GetPolicyWarning(cmPolicies::CMP0175)));
        }
      }
      keywordExpectingValue = nullptr;
      keywordsSeen.insert(copy);

      if (copy == keySOURCE) {
        doing = doing_source;
        keywordExpectingValue = &keySOURCE;
      } else if (copy == keyCOMMAND) {
        doing = doing_command;

        // Save the current command before starting the next command.
        if (!currentLine.empty()) {
          commandLines.push_back(currentLine);
          currentLine.clear();
        }
      } else if (copy == keyPRE_BUILD) {
        cctype = cmCustomCommandType::PRE_BUILD;
      } else if (copy == keyPRE_LINK) {
        cctype = cmCustomCommandType::PRE_LINK;
      } else if (copy == keyPOST_BUILD) {
        cctype = cmCustomCommandType::POST_BUILD;
      } else if (copy == keyVERBATIM) {
        verbatim = true;
      } else if (copy == keyAPPEND) {
        append = true;
      } else if (copy == keyUSES_TERMINAL) {
        uses_terminal = true;
      } else if (copy == keyCOMMAND_EXPAND_LISTS) {
        command_expand_lists = true;
      } else if (copy == keyDEPENDS_EXPLICIT_ONLY) {
        depends_explicit_only = true;
      } else if (copy == keyCODEGEN) {
        codegen = true;
      } else if (copy == keyTARGET) {
        doing = doing_target;
        keywordExpectingValue = &keyTARGET;
      } else if (copy == keyARGS) {
        // Ignore this old keyword.
      } else if (copy == keyDEPENDS) {
        doing = doing_depends;
      } else if (copy == keyOUTPUTS) {
        doing = doing_outputs;
      } else if (copy == keyOUTPUT) {
        doing = doing_output;
        keywordExpectingValue = &keyOUTPUT;
      } else if (copy == keyBYPRODUCTS) {
        doing = doing_byproducts;
      } else if (copy == keyWORKING_DIRECTORY) {
        doing = doing_working_directory;
        keywordExpectingValue = &keyWORKING_DIRECTORY;
      } else if (copy == keyMAIN_DEPENDENCY) {
        doing = doing_main_dependency;
        keywordExpectingValue = &keyMAIN_DEPENDENCY;
      } else if (copy == keyIMPLICIT_DEPENDS) {
        doing = doing_implicit_depends_lang;
      } else if (copy == keyCOMMENT) {
        doing = doing_comment;
        keywordExpectingValue = &keyCOMMENT;
      } else if (copy == keyDEPFILE) {
        doing = doing_depfile;
        if (!mf.GetGlobalGenerator()->SupportsCustomCommandDepfile()) {
          status.SetError(cmStrCat("Option DEPFILE not supported by ",
                                   mf.GetGlobalGenerator()->GetName()));
          return false;
        }
        keywordExpectingValue = &keyDEPFILE;
      } else if (copy == keyJOB_POOL) {
        doing = doing_job_pool;
        keywordExpectingValue = &keyJOB_POOL;
      } else if (copy == keyJOB_SERVER_AWARE) {
        doing = doing_job_server_aware;
        keywordExpectingValue = &keyJOB_SERVER_AWARE;
      }
    } else {
      keywordExpectingValue = nullptr; // Value is being processed now
      std::string filename;
      switch (doing) {
        case doing_output:
        case doing_outputs:
        case doing_byproducts:
          if (!cmSystemTools::FileIsFullPath(copy) &&
              cmGeneratorExpression::Find(copy) != 0) {
            // This is an output to be generated, so it should be
            // under the build tree.
            filename = cmStrCat(mf.GetCurrentBinaryDirectory(), '/');
          }
          filename += copy;
          cmSystemTools::ConvertToUnixSlashes(filename);
          break;
        case doing_source:
        // We do not want to convert the argument to SOURCE because
        // that option is only available for backward compatibility.
        // Old-style use of this command may use the SOURCE==TARGET
        // trick which we must preserve.  If we convert the source
        // to a full path then it will no longer equal the target.
        default:
          break;
      }

      if (cmSystemTools::FileIsFullPath(filename)) {
        filename = cmSystemTools::CollapseFullPath(filename);
      }
      switch (doing) {
        case doing_depfile:
          depfile = copy;
          break;
        case doing_job_pool:
          job_pool = copy;
          break;
        case doing_job_server_aware:
          job_server_aware = copy;
          break;
        case doing_working_directory:
          working = copy;
          break;
        case doing_source:
          source = copy;
          break;
        case doing_output:
          output.push_back(filename);
          break;
        case doing_main_dependency:
          main_dependency = copy;
          break;
        case doing_implicit_depends_lang:
          implicit_depends_lang = copy;
          doing = doing_implicit_depends_file;
          break;
        case doing_implicit_depends_file: {
          // An implicit dependency starting point is also an
          // explicit dependency.
          std::string dep = copy;
          // Upfront path conversion is correct because Genex
          // are not supported.
          cmSystemTools::ConvertToUnixSlashes(dep);
          depends.push_back(dep);

          // Add the implicit dependency language and file.
          implicit_depends.emplace_back(implicit_depends_lang, dep);

          // Switch back to looking for a language.
          doing = doing_implicit_depends_lang;
        } break;
        case doing_command:
          currentLine.push_back(copy);
          break;
        case doing_target:
          target = copy;
          break;
        case doing_depends:
          depends.push_back(copy);
          break;
        case doing_outputs:
          outputs.push_back(filename);
          break;
        case doing_byproducts:
          byproducts.push_back(filename);
          break;
        case doing_comment:
          if (!comment_buffer.empty()) {
            std::string const msg =
              "COMMENT requires exactly one argument, but multiple values "
              "or COMMENT keywords have been given.";
            if (cmp0175 == cmPolicies::NEW) {
              mf.IssueMessage(MessageType::FATAL_ERROR, msg);
              return false;
            }
            if (cmp0175 == cmPolicies::WARN) {
              mf.IssueMessage(
                MessageType::AUTHOR_WARNING,
                cmStrCat(msg, '\n',
                         cmPolicies::GetPolicyWarning(cmPolicies::CMP0175)));
            }
          }
          comment_buffer = copy;
          comment = comment_buffer.c_str();
          break;
        default:
          status.SetError("Wrong syntax. Unknown type of argument.");
          return false;
      }
    }
  }

  // Store the last command line finished.
  if (!currentLine.empty()) {
    commandLines.push_back(currentLine);
    currentLine.clear();
  }

  // At this point we could complain about the lack of arguments.  For
  // the moment, let's say that COMMAND, TARGET are always required.
  if (output.empty() && target.empty()) {
    status.SetError("Wrong syntax. A TARGET or OUTPUT must be specified.");
    return false;
  }

  if (source.empty() && !target.empty() && !output.empty()) {
    status.SetError(
      "Wrong syntax. A TARGET and OUTPUT can not both be specified.");
    return false;
  }
  if (append && output.empty()) {
    status.SetError("given APPEND option with no OUTPUT.");
    return false;
  }
  if (!implicit_depends.empty() && !depfile.empty() &&
      mf.GetGlobalGenerator()->GetName() != "Ninja") {
    // Makefiles generators does not support both at the same time
    status.SetError("IMPLICIT_DEPENDS and DEPFILE can not both be specified.");
    return false;
  }

  if (codegen) {
    if (output.empty()) {
      status.SetError("CODEGEN requires at least 1 OUTPUT.");
      return false;
    }

    if (append) {
      status.SetError("CODEGEN may not be used with APPEND.");
      return false;
    }

    if (!implicit_depends.empty()) {
      status.SetError("CODEGEN is not compatible with IMPLICIT_DEPENDS.");
      return false;
    }

    if (mf.GetPolicyStatus(cmPolicies::CMP0171) != cmPolicies::NEW) {
      status.SetError("CODEGEN option requires policy CMP0171 be set to NEW!");
      return false;
    }
  }

  // Check for an append request.
  if (append) {
    std::vector<std::string> unsupportedKeywordsUsed;
    std::set_difference(keywordsSeen.begin(), keywordsSeen.end(),
                        supportedAppendKeywords.begin(),
                        supportedAppendKeywords.end(),
                        std::back_inserter(unsupportedKeywordsUsed));
    if (!unsupportedKeywordsUsed.empty()) {
      std::string const msg =
        cmJoin(unsupportedKeywordsUsed, ", "_s,
               "The following keywords are not supported when using "
               "APPEND with add_custom_command(OUTPUT): "_s);
      if (cmp0175 == cmPolicies::NEW) {
        mf.IssueMessage(MessageType::FATAL_ERROR, msg);
        return false;
      }
      if (cmp0175 == cmPolicies::WARN) {
        mf.IssueMessage(
          MessageType::AUTHOR_WARNING,
          cmStrCat(msg, ".\n",
                   cmPolicies::GetPolicyWarning(cmPolicies::CMP0175)));
      }
    }
    mf.AppendCustomCommandToOutput(output[0], depends, implicit_depends,
                                   commandLines);
    return true;
  }

  if (uses_terminal && !job_pool.empty()) {
    status.SetError("JOB_POOL is shadowed by USES_TERMINAL.");
    return false;
  }

  // Choose which mode of the command to use.
  auto cc = cm::make_unique<cmCustomCommand>();
  cc->SetByproducts(byproducts);
  cc->SetCommandLines(commandLines);
  cc->SetComment(comment);
  cc->SetWorkingDirectory(working.c_str());
  cc->SetEscapeOldStyle(!verbatim);
  cc->SetUsesTerminal(uses_terminal);
  cc->SetDepfile(depfile);
  cc->SetJobPool(job_pool);
  cc->SetJobserverAware(cmIsOn(job_server_aware));
  cc->SetCommandExpandLists(command_expand_lists);
  cc->SetDependsExplicitOnly(depends_explicit_only);
  if (source.empty() && output.empty()) {
    // Source is empty, use the target.
    if (commandLines.empty()) {
      std::string const msg = "At least one COMMAND must be given.";
      if (cmp0175 == cmPolicies::NEW) {
        mf.IssueMessage(MessageType::FATAL_ERROR, msg);
        return false;
      }
      if (cmp0175 == cmPolicies::WARN) {
        mf.IssueMessage(
          MessageType::AUTHOR_WARNING,
          cmStrCat(msg, '\n',
                   cmPolicies::GetPolicyWarning(cmPolicies::CMP0175)));
      }
    }

    std::vector<std::string> unsupportedKeywordsUsed;
    std::set_difference(keywordsSeen.begin(), keywordsSeen.end(),
                        supportedTargetKeywords.begin(),
                        supportedTargetKeywords.end(),
                        std::back_inserter(unsupportedKeywordsUsed));
    if (!unsupportedKeywordsUsed.empty()) {
      std::string const msg =
        cmJoin(unsupportedKeywordsUsed, ", "_s,
               "The following keywords are not supported when using "
               "add_custom_command(TARGET): "_s);
      if (cmp0175 == cmPolicies::NEW) {
        mf.IssueMessage(MessageType::FATAL_ERROR, msg);
        return false;
      }
      if (cmp0175 == cmPolicies::WARN) {
        mf.IssueMessage(
          MessageType::AUTHOR_WARNING,
          cmStrCat(msg, ".\n",
                   cmPolicies::GetPolicyWarning(cmPolicies::CMP0175)));
      }
    }
    auto const prePostCount = keywordsSeen.count(keyPRE_BUILD) +
      keywordsSeen.count(keyPRE_LINK) + keywordsSeen.count(keyPOST_BUILD);
    if (prePostCount != 1) {
      std::string msg =
        "Exactly one of PRE_BUILD, PRE_LINK, or POST_BUILD must be given.";
      if (cmp0175 == cmPolicies::NEW) {
        mf.IssueMessage(MessageType::FATAL_ERROR, msg);
        return false;
      }
      if (cmp0175 == cmPolicies::WARN) {
        msg += " Assuming ";
        switch (cctype) {
          case cmCustomCommandType::PRE_BUILD:
            msg += "PRE_BUILD";
            break;
          case cmCustomCommandType::PRE_LINK:
            msg += "PRE_LINK";
            break;
          case cmCustomCommandType::POST_BUILD:
            msg += "POST_BUILD";
        }
        mf.IssueMessage(
          MessageType::AUTHOR_WARNING,
          cmStrCat(msg, " to preserve backward compatibility.\n",
                   cmPolicies::GetPolicyWarning(cmPolicies::CMP0175)));
      }
    }
    mf.AddCustomCommandToTarget(target, cctype, std::move(cc));
  } else if (target.empty()) {
    // Target is empty, use the output.
    std::vector<std::string> unsupportedKeywordsUsed;
    std::set_difference(keywordsSeen.begin(), keywordsSeen.end(),
                        supportedOutputKeywords.begin(),
                        supportedOutputKeywords.end(),
                        std::back_inserter(unsupportedKeywordsUsed));
    if (!unsupportedKeywordsUsed.empty()) {
      std::string const msg =
        cmJoin(unsupportedKeywordsUsed, ", "_s,
               "The following keywords are not supported when using "
               "add_custom_command(OUTPUT): "_s);
      if (cmp0175 == cmPolicies::NEW) {
        mf.IssueMessage(MessageType::FATAL_ERROR, msg);
        return false;
      }
      if (cmp0175 == cmPolicies::WARN) {
        mf.IssueMessage(
          MessageType::AUTHOR_WARNING,
          cmStrCat(msg, ".\n",
                   cmPolicies::GetPolicyWarning(cmPolicies::CMP0175)));
      }
    }
    cc->SetOutputs(output);
    cc->SetMainDependency(main_dependency);
    cc->SetDepends(depends);
    cc->SetCodegen(codegen);
    cc->SetImplicitDepends(implicit_depends);
    mf.AddCustomCommandToOutput(std::move(cc));
  } else {
    mf.IssueMessage(
      MessageType::FATAL_ERROR,
      "The SOURCE signatures of add_custom_command are no longer supported.");
    return false;
  }

  return true;
}
