/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#include "cmVisualStudioSlnParser.h"

#include <cassert>
#include <memory>
#include <stack>
#include <utility>
#include <vector>

#include <cmext/string_view>

#include "cmsys/FStream.hxx"

#include "cmStringAlgorithms.h"
#include "cmSystemTools.h"
#include "cmVisualStudioSlnData.h"

namespace {
enum LineFormat
{
  LineMultiValueTag,
  LineSingleValueTag,
  LineKeyValuePair,
  LineVerbatim
};
}

class cmVisualStudioSlnParser::ParsedLine
{
public:
  bool IsComment() const;
  bool IsKeyValuePair() const;

  std::string const& GetTag() const { return this->Tag; }
  std::string const& GetArg() const { return this->Arg.first; }
  std::string GetArgVerbatim() const;
  size_t GetValueCount() const { return this->Values.size(); }
  std::string const& GetValue(size_t idxValue) const;
  std::string GetValueVerbatim(size_t idxValue) const;

  void SetTag(std::string const& tag) { this->Tag = tag; }
  void SetArg(std::string const& arg) { this->Arg = StringData(arg, false); }
  void SetQuotedArg(std::string const& arg)
  {
    this->Arg = StringData(arg, true);
  }
  void AddValue(std::string const& value)
  {
    this->Values.push_back(StringData(value, false));
  }
  void AddQuotedValue(std::string const& value)
  {
    this->Values.push_back(StringData(value, true));
  }

  void CopyVerbatim(std::string const& line) { this->Tag = line; }

private:
  using StringData = std::pair<std::string, bool>;
  std::string Tag;
  StringData Arg;
  std::vector<StringData> Values;
  static std::string const BadString;
  static std::string const Quote;
};

std::string const cmVisualStudioSlnParser::ParsedLine::BadString;
std::string const cmVisualStudioSlnParser::ParsedLine::Quote("\"");

bool cmVisualStudioSlnParser::ParsedLine::IsComment() const
{
  assert(!this->Tag.empty());
  return (this->Tag[0] == '#');
}

bool cmVisualStudioSlnParser::ParsedLine::IsKeyValuePair() const
{
  assert(!this->Tag.empty());
  return this->Arg.first.empty() && this->Values.size() == 1;
}

std::string cmVisualStudioSlnParser::ParsedLine::GetArgVerbatim() const
{
  if (this->Arg.second) {
    return cmStrCat(Quote, this->Arg.first, Quote);
  }
  return this->Arg.first;
}

std::string const& cmVisualStudioSlnParser::ParsedLine::GetValue(
  size_t idxValue) const
{
  if (idxValue < this->Values.size()) {
    return this->Values[idxValue].first;
  }
  return BadString;
}

std::string cmVisualStudioSlnParser::ParsedLine::GetValueVerbatim(
  size_t idxValue) const
{
  if (idxValue < this->Values.size()) {
    StringData const& data = this->Values[idxValue];
    if (data.second) {
      return cmStrCat(Quote, data.first, Quote);
    }
    return data.first;
  }
  return BadString;
}

class cmVisualStudioSlnParser::State
{
public:
  explicit State(DataGroupSet requestedData);

  size_t GetCurrentLine() const { return this->CurrentLine; }
  bool ReadLine(std::istream& input, std::string& line);

  LineFormat NextLineFormat() const;

  bool Process(cmVisualStudioSlnParser::ParsedLine const& line,
               cmSlnData& output, cmVisualStudioSlnParser::ResultData& result);

  bool Finished(cmVisualStudioSlnParser::ResultData& result);

private:
  enum FileState
  {
    FileStateStart,
    FileStateTopLevel,
    FileStateProject,
    FileStateProjectDependencies,
    FileStateGlobal,
    FileStateSolutionConfigurations,
    FileStateProjectConfigurations,
    FileStateSolutionFilters,
    FileStateGlobalSection,
    FileStateIgnore
  };
  std::stack<FileState> Stack;
  std::string EndIgnoreTag;
  DataGroupSet RequestedData;
  size_t CurrentLine = 0;

  void IgnoreUntilTag(std::string const& endTag);
};

cmVisualStudioSlnParser::State::State(DataGroupSet requestedData)
  : RequestedData(requestedData)
{
  if (this->RequestedData.test(DataGroupProjectDependenciesBit)) {
    this->RequestedData.set(DataGroupProjectsBit);
  }
  this->Stack.push(FileStateStart);
}

bool cmVisualStudioSlnParser::State::ReadLine(std::istream& input,
                                              std::string& line)
{
  ++this->CurrentLine;
  return !std::getline(input, line).fail();
}

LineFormat cmVisualStudioSlnParser::State::NextLineFormat() const
{
  switch (this->Stack.top()) {
    case FileStateStart:
      return LineVerbatim;
    case FileStateTopLevel:
      return LineMultiValueTag;
    case FileStateProject:
    case FileStateGlobal:
      return LineSingleValueTag;
    case FileStateProjectDependencies:
    case FileStateSolutionConfigurations:
    case FileStateProjectConfigurations:
    case FileStateSolutionFilters:
    case FileStateGlobalSection:
      return LineKeyValuePair;
    case FileStateIgnore:
      return LineVerbatim;
    default:
      assert(false);
      return LineVerbatim;
  }
}

bool cmVisualStudioSlnParser::State::Process(
  cmVisualStudioSlnParser::ParsedLine const& line, cmSlnData& output,
  cmVisualStudioSlnParser::ResultData& result)
{
  assert(!line.IsComment());
  switch (this->Stack.top()) {
    case FileStateStart:
      if (!cmHasLiteralPrefix(line.GetTag(),
                              "Microsoft Visual Studio Solution File")) {
        result.SetError(ResultErrorInputStructure, this->GetCurrentLine());
        return false;
      }
      this->Stack.pop();
      this->Stack.push(FileStateTopLevel);
      break;
    case FileStateTopLevel:
      if (line.GetTag() == "Project"_s) {
        if (line.GetValueCount() != 3) {
          result.SetError(ResultErrorInputStructure, this->GetCurrentLine());
          return false;
        }
        if (this->RequestedData.test(DataGroupProjectsBit)) {
          if (!output.AddProject(line.GetValue(2), line.GetValue(0),
                                 line.GetValue(1))) {
            result.SetError(ResultErrorInputData, this->GetCurrentLine());
            return false;
          }
          this->Stack.push(FileStateProject);
        } else {
          this->IgnoreUntilTag("EndProject");
        }
      } else if (line.GetTag() == "Global"_s) {

        this->Stack.push(FileStateGlobal);
      } else if (line.GetTag() == "VisualStudioVersion"_s) {
        output.SetVisualStudioVersion(line.GetValue(0));
      } else if (line.GetTag() == "MinimumVisualStudioVersion"_s) {
        output.SetMinimumVisualStudioVersion(line.GetValue(0));
      } else {
        result.SetError(ResultErrorInputStructure, this->GetCurrentLine());
        return false;
      }
      break;
    case FileStateProject:
      if (line.GetTag() == "EndProject"_s) {
        this->Stack.pop();
      } else if (line.GetTag() == "ProjectSection"_s) {
        if (line.GetArg() == "ProjectDependencies"_s &&
            line.GetValue(0) == "postProject"_s) {
          if (this->RequestedData.test(DataGroupProjectDependenciesBit)) {
            this->Stack.push(FileStateProjectDependencies);
          } else {
            this->IgnoreUntilTag("EndProjectSection");
          }
        } else {
          this->IgnoreUntilTag("EndProjectSection");
        }
      } else {
        result.SetError(ResultErrorInputStructure, this->GetCurrentLine());
        return false;
      }
      break;
    case FileStateProjectDependencies:
      if (line.GetTag() == "EndProjectSection"_s) {
        this->Stack.pop();
      } else if (line.IsKeyValuePair()) {
        // implement dependency storing here, once needed
        ;
      } else {
        result.SetError(ResultErrorInputStructure, this->GetCurrentLine());
        return false;
      }
      break;
    case FileStateGlobal:
      if (line.GetTag() == "EndGlobal"_s) {
        this->Stack.pop();
      } else if (line.GetTag() == "GlobalSection"_s) {
        if (line.GetArg() == "SolutionConfigurationPlatforms"_s &&
            line.GetValue(0) == "preSolution"_s) {
          if (this->RequestedData.test(DataGroupSolutionConfigurationsBit)) {
            this->Stack.push(FileStateSolutionConfigurations);
          } else {
            this->IgnoreUntilTag("EndGlobalSection");
          }
        } else if (line.GetArg() == "ProjectConfigurationPlatforms"_s &&
                   line.GetValue(0) == "postSolution"_s) {
          if (this->RequestedData.test(DataGroupProjectConfigurationsBit)) {
            this->Stack.push(FileStateProjectConfigurations);
          } else {
            this->IgnoreUntilTag("EndGlobalSection");
          }
        } else if (line.GetArg() == "NestedProjects"_s &&
                   line.GetValue(0) == "preSolution"_s) {
          if (this->RequestedData.test(DataGroupSolutionFiltersBit)) {
            this->Stack.push(FileStateSolutionFilters);
          } else {
            this->IgnoreUntilTag("EndGlobalSection");
          }
        } else if (this->RequestedData.test(
                     DataGroupGenericGlobalSectionsBit)) {
          this->Stack.push(FileStateGlobalSection);
        } else {
          this->IgnoreUntilTag("EndGlobalSection");
        }
      } else {
        result.SetError(ResultErrorInputStructure, this->GetCurrentLine());
        return false;
      }
      break;
    case FileStateSolutionConfigurations:
      if (line.GetTag() == "EndGlobalSection"_s) {
        this->Stack.pop();
      } else if (line.IsKeyValuePair()) {
        output.AddConfiguration(line.GetValue(0));
      } else {
        result.SetError(ResultErrorInputStructure, this->GetCurrentLine());
        return false;
      }
      break;
    case FileStateProjectConfigurations:
      if (line.GetTag() == "EndGlobalSection"_s) {
        this->Stack.pop();
      } else if (line.IsKeyValuePair()) {
        std::vector<std::string> tagElements =
          cmSystemTools::SplitString(line.GetTag(), '.');
        if (tagElements.size() != 3 && tagElements.size() != 4) {
          result.SetError(ResultErrorInputStructure, this->GetCurrentLine());
          return false;
        }

        std::string guid = tagElements[0];
        std::string solutionConfiguration = tagElements[1];
        std::string activeBuild = tagElements[2];
        cm::optional<cmSlnProjectEntry> projectEntry =
          output.GetProjectByGUID(guid);

        if (!projectEntry) {
          result.SetError(ResultErrorInputStructure, this->GetCurrentLine());
          return false;
        }

        if (activeBuild == "ActiveCfg"_s) {
          projectEntry->AddProjectConfiguration(solutionConfiguration,
                                                line.GetValue(0));
        }
      } else {
        result.SetError(ResultErrorInputStructure, this->GetCurrentLine());
        return false;
      }
      break;
    case FileStateSolutionFilters:
      if (line.GetTag() == "EndGlobalSection"_s) {
        this->Stack.pop();
      } else if (line.IsKeyValuePair()) {
        // implement filter storing here, once needed
        ;
      } else {
        result.SetError(ResultErrorInputStructure, this->GetCurrentLine());
        return false;
      }
      break;
    case FileStateGlobalSection:
      if (line.GetTag() == "EndGlobalSection"_s) {
        this->Stack.pop();
      } else if (line.IsKeyValuePair()) {
        // implement section storing here, once needed
        ;
      } else {
        result.SetError(ResultErrorInputStructure, this->GetCurrentLine());
        return false;
      }
      break;
    case FileStateIgnore:
      if (line.GetTag() == this->EndIgnoreTag) {
        this->Stack.pop();
        this->EndIgnoreTag.clear();
      }
      break;
    default:
      result.SetError(ResultErrorBadInternalState, this->GetCurrentLine());
      return false;
  }
  return true;
}

bool cmVisualStudioSlnParser::State::Finished(
  cmVisualStudioSlnParser::ResultData& result)
{
  if (this->Stack.top() != FileStateTopLevel) {
    result.SetError(ResultErrorInputStructure, this->GetCurrentLine());
    return false;
  }
  result.Result = ResultOK;
  return true;
}

void cmVisualStudioSlnParser::State::IgnoreUntilTag(std::string const& endTag)
{
  this->Stack.push(FileStateIgnore);
  this->EndIgnoreTag = endTag;
}

cmVisualStudioSlnParser::ResultData::ResultData() = default;

void cmVisualStudioSlnParser::ResultData::Clear()
{
  *this = ResultData();
}

void cmVisualStudioSlnParser::ResultData::SetError(ParseResult error,
                                                   size_t line)
{
  this->Result = error;
  this->ResultLine = line;
}

cmVisualStudioSlnParser::DataGroupSet const
  cmVisualStudioSlnParser::DataGroupProjects(
    1 << cmVisualStudioSlnParser::DataGroupProjectsBit);

cmVisualStudioSlnParser::DataGroupSet const
  cmVisualStudioSlnParser::DataGroupProjectDependencies(
    1 << cmVisualStudioSlnParser::DataGroupProjectDependenciesBit);

cmVisualStudioSlnParser::DataGroupSet const
  cmVisualStudioSlnParser::DataGroupSolutionConfigurations(
    1 << cmVisualStudioSlnParser::DataGroupSolutionConfigurationsBit);

cmVisualStudioSlnParser::DataGroupSet const
  cmVisualStudioSlnParser::DataGroupProjectConfigurations(
    1 << cmVisualStudioSlnParser::DataGroupProjectConfigurationsBit);

cmVisualStudioSlnParser::DataGroupSet const
  cmVisualStudioSlnParser::DataGroupSolutionFilters(
    1 << cmVisualStudioSlnParser::DataGroupSolutionFiltersBit);

cmVisualStudioSlnParser::DataGroupSet const
  cmVisualStudioSlnParser::DataGroupGenericGlobalSections(
    1 << cmVisualStudioSlnParser::DataGroupGenericGlobalSectionsBit);

cmVisualStudioSlnParser::DataGroupSet const
  cmVisualStudioSlnParser::DataGroupAll(~0);

bool cmVisualStudioSlnParser::Parse(std::istream& input, cmSlnData& output,
                                    DataGroupSet dataGroups)
{
  this->LastResult.Clear();
  if (!this->IsDataGroupSetSupported(dataGroups)) {
    this->LastResult.SetError(ResultErrorUnsupportedDataGroup, 0);
    return false;
  }
  State state(dataGroups);
  return this->ParseImpl(input, output, state);
}

bool cmVisualStudioSlnParser::ParseFile(std::string const& file,
                                        cmSlnData& output,
                                        DataGroupSet dataGroups)
{
  this->LastResult.Clear();
  if (!this->IsDataGroupSetSupported(dataGroups)) {
    this->LastResult.SetError(ResultErrorUnsupportedDataGroup, 0);
    return false;
  }
  cmsys::ifstream f(file.c_str());
  if (!f) {
    this->LastResult.SetError(ResultErrorOpeningInput, 0);
    return false;
  }
  State state(dataGroups);
  return this->ParseImpl(f, output, state);
}

cmVisualStudioSlnParser::ParseResult cmVisualStudioSlnParser::GetParseResult()
  const
{
  return this->LastResult.Result;
}

size_t cmVisualStudioSlnParser::GetParseResultLine() const
{
  return this->LastResult.ResultLine;
}

bool cmVisualStudioSlnParser::GetParseHadBOM() const
{
  return this->LastResult.HadBOM;
}

bool cmVisualStudioSlnParser::IsDataGroupSetSupported(
  DataGroupSet dataGroups) const
{
  return (dataGroups & DataGroupProjects) != 0;
}

bool cmVisualStudioSlnParser::ParseImpl(std::istream& input, cmSlnData& output,
                                        State& state)
{
  std::string line;
  // Does the .sln start with a Byte Order Mark?
  if (!this->ParseBOM(input, line, state)) {
    return false;
  }
  do {
    line = cmTrimWhitespace(line);
    if (line.empty()) {
      continue;
    }
    ParsedLine parsedLine;
    switch (state.NextLineFormat()) {
      case LineMultiValueTag:
        if (!this->ParseMultiValueTag(line, parsedLine, state)) {
          return false;
        }
        break;
      case LineSingleValueTag:
        if (!this->ParseSingleValueTag(line, parsedLine, state)) {
          return false;
        }
        break;
      case LineKeyValuePair:
        if (!this->ParseKeyValuePair(line, parsedLine, state)) {
          return false;
        }
        break;
      case LineVerbatim:
        parsedLine.CopyVerbatim(line);
        break;
    }
    if (parsedLine.IsComment()) {
      continue;
    }
    if (!state.Process(parsedLine, output, this->LastResult)) {
      return false;
    }
  } while (state.ReadLine(input, line));
  return state.Finished(this->LastResult);
}

bool cmVisualStudioSlnParser::ParseBOM(std::istream& input, std::string& line,
                                       State& state)
{
  char bom[4];
  if (!input.get(bom, 4)) {
    this->LastResult.SetError(ResultErrorReadingInput, 1);
    return false;
  }
  this->LastResult.HadBOM =
    (bom[0] == char(0xEF) && bom[1] == char(0xBB) && bom[2] == char(0xBF));
  if (!state.ReadLine(input, line)) {
    this->LastResult.SetError(ResultErrorReadingInput, 1);
    return false;
  }
  if (!this->LastResult.HadBOM) {
    line = cmStrCat(bom, line); // it wasn't a BOM, prepend it to first line
  }
  return true;
}

bool cmVisualStudioSlnParser::ParseMultiValueTag(std::string const& line,
                                                 ParsedLine& parsedLine,
                                                 State& state)
{
  size_t idxEqualSign = line.find('=');
  auto fullTag = cm::string_view(line).substr(0, idxEqualSign);
  if (!this->ParseTag(fullTag, parsedLine, state)) {
    return false;
  }
  if (idxEqualSign != std::string::npos) {
    size_t idxFieldStart = idxEqualSign + 1;
    if (idxFieldStart < line.size()) {
      size_t idxParsing = idxFieldStart;
      bool inQuotes = false;
      for (;;) {
        idxParsing = line.find_first_of(",\"", idxParsing);
        bool fieldOver = false;
        if (idxParsing == std::string::npos) {
          fieldOver = true;
          if (inQuotes) {
            this->LastResult.SetError(ResultErrorInputStructure,
                                      state.GetCurrentLine());
            return false;
          }
        } else if (line[idxParsing] == ',' && !inQuotes) {
          fieldOver = true;
        } else if (line[idxParsing] == '"') {
          inQuotes = !inQuotes;
        }
        if (fieldOver) {
          if (!this->ParseValue(
                line.substr(idxFieldStart, idxParsing - idxFieldStart),
                parsedLine)) {
            return false;
          }
          if (idxParsing == std::string::npos) {
            break; // end of last field
          }
          idxFieldStart = idxParsing + 1;
        }
        ++idxParsing;
      }
    }
  }
  return true;
}

bool cmVisualStudioSlnParser::ParseSingleValueTag(std::string const& line,
                                                  ParsedLine& parsedLine,
                                                  State& state)
{
  size_t idxEqualSign = line.find('=');
  auto fullTag = cm::string_view(line).substr(0, idxEqualSign);
  if (!this->ParseTag(fullTag, parsedLine, state)) {
    return false;
  }
  if (idxEqualSign != std::string::npos) {
    if (!this->ParseValue(line.substr(idxEqualSign + 1), parsedLine)) {
      return false;
    }
  }
  return true;
}

bool cmVisualStudioSlnParser::ParseKeyValuePair(std::string const& line,
                                                ParsedLine& parsedLine,
                                                State& /*state*/)
{
  size_t idxEqualSign = line.find('=');
  if (idxEqualSign == std::string::npos) {
    parsedLine.CopyVerbatim(line);
    return true;
  }
  std::string const& key = line.substr(0, idxEqualSign);
  parsedLine.SetTag(cmTrimWhitespace(key));
  std::string const& value = line.substr(idxEqualSign + 1);
  parsedLine.AddValue(cmTrimWhitespace(value));
  return true;
}

bool cmVisualStudioSlnParser::ParseTag(cm::string_view fullTag,
                                       ParsedLine& parsedLine, State& state)
{
  size_t idxLeftParen = fullTag.find('(');
  if (idxLeftParen == cm::string_view::npos) {
    parsedLine.SetTag(cmTrimWhitespace(fullTag));
    return true;
  }
  parsedLine.SetTag(cmTrimWhitespace(fullTag.substr(0, idxLeftParen)));
  size_t idxRightParen = fullTag.rfind(')');
  if (idxRightParen == cm::string_view::npos) {
    this->LastResult.SetError(ResultErrorInputStructure,
                              state.GetCurrentLine());
    return false;
  }
  std::string const& arg = cmTrimWhitespace(
    fullTag.substr(idxLeftParen + 1, idxRightParen - idxLeftParen - 1));
  if (arg.front() == '"') {
    if (arg.back() != '"') {
      this->LastResult.SetError(ResultErrorInputStructure,
                                state.GetCurrentLine());
      return false;
    }
    parsedLine.SetQuotedArg(arg.substr(1, arg.size() - 2));
  } else {
    parsedLine.SetArg(arg);
  }
  return true;
}

bool cmVisualStudioSlnParser::ParseValue(std::string const& value,
                                         ParsedLine& parsedLine)
{
  std::string const& trimmed = cmTrimWhitespace(value);
  if (trimmed.empty()) {
    parsedLine.AddValue(trimmed);
  } else if (trimmed.front() == '"' && trimmed.back() == '"') {
    parsedLine.AddQuotedValue(trimmed.substr(1, trimmed.size() - 2));
  } else {
    parsedLine.AddValue(trimmed);
  }
  return true;
}
