/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#include "cmExportTryCompileFileGenerator.h"

#include <map>
#include <utility>

#include <cm/memory>
#include <cm/string_view>

#include "cmFileSet.h"
#include "cmGeneratorExpression.h"
#include "cmGeneratorExpressionDAGChecker.h"
#include "cmGeneratorTarget.h"
#include "cmGlobalGenerator.h"
#include "cmList.h"
#include "cmLocalGenerator.h"
#include "cmMakefile.h"
#include "cmOutputConverter.h"
#include "cmStateTypes.h"
#include "cmStringAlgorithms.h"
#include "cmSystemTools.h"
#include "cmTarget.h"
#include "cmValue.h"

class cmTargetExport;

cmExportTryCompileFileGenerator::cmExportTryCompileFileGenerator(
  cmGlobalGenerator* gg, std::vector<std::string> const& targets,
  cmMakefile* mf, std::set<std::string> const& langs)
  : Languages(langs.begin(), langs.end())
{
  gg->CreateImportedGenerationObjects(mf, targets, this->Exports);
}

void cmExportTryCompileFileGenerator::ReportError(
  std::string const& errorMessage) const
{
  cmSystemTools::Error(errorMessage);
}

bool cmExportTryCompileFileGenerator::GenerateMainFile(std::ostream& os)
{
  std::set<cmGeneratorTarget const*> emitted;
  std::set<cmGeneratorTarget const*> emittedDeps;
  while (!this->Exports.empty()) {
    cmGeneratorTarget const* te = this->Exports.back();
    this->Exports.pop_back();
    if (emitted.insert(te).second) {
      emittedDeps.insert(te);
      this->GenerateImportTargetCode(os, te, te->GetType());

      ImportPropertyMap properties;

      for (std::string const& lang : this->Languages) {
        for (auto i : cmGeneratorTarget::BuiltinTransitiveProperties) {
          this->FindTargets(std::string(i.second.InterfaceName), te, lang,
                            emittedDeps);
        }
      }

      this->PopulateProperties(te, properties, emittedDeps);

      this->GenerateInterfaceProperties(te, os, properties);
    }
  }
  return true;
}

std::string cmExportTryCompileFileGenerator::FindTargets(
  std::string const& propName, cmGeneratorTarget const* tgt,
  std::string const& language, std::set<cmGeneratorTarget const*>& emitted)
{
  cmValue prop = tgt->GetProperty(propName);
  if (!prop) {
    return std::string();
  }

  cmGeneratorExpression ge(*tgt->Makefile->GetCMakeInstance());

  std::unique_ptr<cmGeneratorExpressionDAGChecker> parentDagChecker;
  if (propName == "INTERFACE_LINK_OPTIONS") {
    // To please constraint checks of DAGChecker, this property must have
    // LINK_OPTIONS property as parent
    parentDagChecker = cm::make_unique<cmGeneratorExpressionDAGChecker>(
      tgt, "LINK_OPTIONS", nullptr, nullptr, tgt->GetLocalGenerator(),
      this->Config);
  }
  cmGeneratorExpressionDAGChecker dagChecker{
    tgt,
    propName,
    nullptr,
    parentDagChecker.get(),
    tgt->GetLocalGenerator(),
    this->Config,
  };

  std::unique_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(*prop);

  cmTarget dummyHead("try_compile_dummy_exe", cmStateEnums::EXECUTABLE,
                     cmTarget::Visibility::Normal, tgt->Target->GetMakefile(),
                     cmTarget::PerConfig::Yes);

  cmGeneratorTarget gDummyHead(&dummyHead, tgt->GetLocalGenerator());

  std::string result = cge->Evaluate(tgt->GetLocalGenerator(), this->Config,
                                     &gDummyHead, &dagChecker, tgt, language);

  std::set<cmGeneratorTarget const*> const& allTargets =
    cge->GetAllTargetsSeen();
  for (cmGeneratorTarget const* target : allTargets) {
    if (emitted.insert(target).second) {
      this->Exports.push_back(target);
    }
  }
  return result;
}

void cmExportTryCompileFileGenerator::PopulateProperties(
  cmGeneratorTarget const* target, ImportPropertyMap& properties,
  std::set<cmGeneratorTarget const*>& emitted)
{
  // Look through all non-special properties.
  std::vector<std::string> props = target->GetPropertyKeys();
  // Include special properties that might be relevant here.
  props.emplace_back("INTERFACE_LINK_LIBRARIES");
  props.emplace_back("INTERFACE_LINK_LIBRARIES_DIRECT");
  props.emplace_back("INTERFACE_LINK_LIBRARIES_DIRECT_EXCLUDE");
  for (std::string const& p : props) {
    cmValue v = target->GetProperty(p);
    if (!v) {
      continue;
    }
    properties[p] = *v;

    if (cmHasLiteralPrefix(p, "IMPORTED_LINK_INTERFACE_LIBRARIES") ||
        cmHasLiteralPrefix(p, "IMPORTED_LINK_DEPENDENT_LIBRARIES") ||
        cmHasLiteralPrefix(p, "INTERFACE_LINK_LIBRARIES")) {
      std::string evalResult =
        this->FindTargets(p, target, std::string(), emitted);

      cmList depends{ evalResult };
      for (std::string const& li : depends) {
        cmGeneratorTarget* tgt =
          target->GetLocalGenerator()->FindGeneratorTargetToUse(li);
        if (tgt && emitted.insert(tgt).second) {
          this->Exports.push_back(tgt);
        }
      }
    }
  }
}

std::string cmExportTryCompileFileGenerator::InstallNameDir(
  cmGeneratorTarget const* target, std::string const& config)
{
  std::string install_name_dir;

  cmMakefile* mf = target->Target->GetMakefile();
  if (mf->IsOn("CMAKE_PLATFORM_HAS_INSTALLNAME")) {
    install_name_dir = target->GetInstallNameDirForBuildTree(config);
  }

  return install_name_dir;
}

std::string cmExportTryCompileFileGenerator::GetFileSetDirectories(
  cmGeneratorTarget* /*gte*/, cmFileSet* fileSet, cmTargetExport const* /*te*/)
{
  return cmOutputConverter::EscapeForCMake(
    cmList::to_string(fileSet->GetDirectoryEntries()));
}

std::string cmExportTryCompileFileGenerator::GetFileSetFiles(
  cmGeneratorTarget* /*gte*/, cmFileSet* fileSet, cmTargetExport const* /*te*/)
{
  return cmOutputConverter::EscapeForCMake(
    cmList::to_string(fileSet->GetFileEntries()));
}
