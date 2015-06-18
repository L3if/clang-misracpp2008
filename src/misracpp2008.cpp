//===-  misracpp2008.cpp - A MISRA C++ 2008 rules checker------------------===//
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the logic for registering and running MISRA C++ 2008
// rules checkers.
//
//===----------------------------------------------------------------------===//

#include "misracpp2008.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/AST.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Regex.h"
#include <cassert>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>

using namespace clang;
using namespace llvm;

namespace misracpp2008 {

typedef std::map<std::string, clang::DiagnosticsEngine::Level> DiagLevelMap;
DiagLevelMap &getDiagnosticLevels();
std::set<std::string> &getEnabledCheckers();
std::set<std::string> &getRegisteredCheckerNames();
std::list<llvm::Regex> &getIgnoredPaths();
bool enableChecker(const std::string &name,
                   clang::DiagnosticsEngine::Level diagLevel);
void dumpRegisteredCheckers(llvm::raw_ostream &OS);
void dumpActiveCheckers(llvm::raw_ostream &OS);

void RuleChecker::setDiagLevel(DiagnosticsEngine::Level diagLevel) {
  this->diagLevel = diagLevel;
}

void RuleChecker::setName(const std::string &name) {
  assert(ruleHeadlines.count(name) > 0 && "Invalid name for a rule!");
  this->name = name;
}

void RuleChecker::setCompilerInstance(CompilerInstance &ci) { this->CI = &ci; }

bool RuleChecker::isInSystemHeader(clang::SourceLocation loc) {
  const SourceManager &sourceManager = CI->getSourceManager();
  return sourceManager.isInSystemHeader(loc);
}

bool RuleChecker::isBuiltIn(clang::SourceLocation loc) {
  const SourceManager &sourceManager = CI->getSourceManager();
  const char *const filename = sourceManager.getPresumedLoc(loc).getFilename();
  return (strcmp(filename, "<built-in>") == 0);
}

bool RuleChecker::isCommandLine(clang::SourceLocation loc) {
  const SourceManager &sourceManager = CI->getSourceManager();
  const char *const filename = sourceManager.getPresumedLoc(loc).getFilename();
  return (strcmp(filename, "<command line>") == 0);
}

bool RuleChecker::doIgnore(clang::SourceLocation loc) {
  if (loc.isInvalid()) {
    return true;
  }
  if (isBuiltIn(loc)) {
    return true;
  }
  if (isCommandLine(loc)) {
    return true;
  }
  if (isInSystemHeader(loc)) {
    return doIgnoreSystemHeaders;
  }

  // Do not check source code locations which are originating from a file.
  auto spellingLocation = CI->getSourceManager().getSpellingLoc(loc);
  auto fileName = CI->getSourceManager().getFilename(spellingLocation);
  if (fileName.empty()) {
    return true;
  }

  // Do not check explicitly unchecked files
  auto &ignoredPaths = getIgnoredPaths();
  for (Regex &regex : ignoredPaths) {
    if (regex.match(fileName)) {
      return true;
    }
  }
  return false;
}

void RuleChecker::reportError(SourceLocation loc) {
  assert(ruleHeadlines.count(name) > 0 && "Invalid name for a rule!");
  reportError(loc, "%0 (MISRA C++ 2008 rule %1)") << ruleHeadlines.at(name)
                                                  << name;
}

std::set<std::string> &getEnabledCheckers() {
  static std::set<std::string> enabledCheckers;
  return enabledCheckers;
}

DiagLevelMap &getDiagnosticLevels() {
  static DiagLevelMap diagLevelMap;
  return diagLevelMap;
}

std::list<llvm::Regex> &getIgnoredPaths() {
  static std::list<llvm::Regex> ignoredPaths;
  return ignoredPaths;
}

bool enableChecker(const std::string &checkerName,
                   clang::DiagnosticsEngine::Level diagLevel) {
  if (getRegisteredCheckerNames().count(checkerName) == 0) {
    return false;
  }
  auto &checkerDiagLevelMap = getDiagnosticLevels();
  checkerDiagLevelMap[checkerName] = diagLevel;
  getEnabledCheckers().insert(checkerName);
  return true;
}

void dumpRegisteredCheckers(raw_ostream &OS) {
  auto checkers = getRegisteredCheckerNames();
  OS << "Registered checks: " << llvm::join(std::begin(checkers),
                                            std::end(checkers), ", ") << "\n";
}

void dumpActiveCheckers(raw_ostream &OS) {
  auto checkers = getEnabledCheckers();
  OS << "Active checks: " << llvm::join(std::begin(checkers),
                                        std::end(checkers), ", ") << "\n";
}

class Consumer : public clang::ASTConsumer {
private:
  clang::CompilerInstance &CI;

public:
  Consumer(clang::CompilerInstance &CI) : CI(CI) {}
  virtual void HandleTranslationUnit(clang::ASTContext &ctx) override {
    // Iterate over registered ASTContext checkers and execute the ones active
    const auto &enabledCheckers = getEnabledCheckers();
    for (RuleCheckerASTContextRegistry::iterator
             it = RuleCheckerASTContextRegistry::begin(),
             ie = RuleCheckerASTContextRegistry::end();
         it != ie; ++it) {
      const std::string checkerName =
          RuleCheckerASTContextRegistry::traits::nameof(*it);
      if (enabledCheckers.count(checkerName) > 0) {
        auto diagLevel = getDiagnosticLevels().at(checkerName);
        auto instance = it->instantiate();
        instance->setCompilerInstance(CI);
        instance->setContext(ctx);
        instance->setDiagLevel(diagLevel);
        instance->setName(checkerName);
        instance->doWork();
      }
    }
  }
};

class Action : public clang::PluginASTAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(clang::CompilerInstance &CI,
                                                 llvm::StringRef) override {
    // Dump the available and activated checkers
    dumpRegisteredCheckers(llvm::outs());
    dumpActiveCheckers(llvm::outs());

    // Iterate over registered preprocessor checkers and execute the ones active
    const auto &enabledCheckers = getEnabledCheckers();
    for (RuleCheckerPreprocessorRegistry::iterator
             it = RuleCheckerPreprocessorRegistry::begin(),
             ie = RuleCheckerPreprocessorRegistry::end();
         it != ie; ++it) {
      const std::string checkerName =
          RuleCheckerPreprocessorRegistry::traits::nameof(*it);
      if (enabledCheckers.count(checkerName) > 0) {
        assert(CI.hasPreprocessor() &&
               "Compiler instance has no preprocessor!");
        auto diagLevel = getDiagnosticLevels().at(checkerName);
        std::unique_ptr<RuleCheckerPPCallback> ppCallback = it->instantiate();
        ppCallback->setDiagLevel(diagLevel);
        ppCallback->setCompilerInstance(CI);
        ppCallback->setName(checkerName);
        CI.getPreprocessor().addPPCallbacks(
            std::unique_ptr<PPCallbacks>(ppCallback.release()));
      }
    }
    return std::unique_ptr<ASTConsumer>(new Consumer(CI));
  }

  virtual bool ParseArgs(const clang::CompilerInstance &CI,
                         const std::vector<std::string> &args) override {
    for (const std::string &currentString : args) {
      // Handle help request
      if (currentString == "--help") {
        PrintHelp(llvm::outs());
        return true;
      }
      // Handle --exclude-path arguments
      const std::string excludeArgument = "--exclude-path=";
      if (auto pos = currentString.find(excludeArgument) != std::string::npos) {
        auto ignorePath = currentString.substr(
            pos + excludeArgument.length() - 1, std::string::npos);
        getIgnoredPaths().push_back(llvm::Regex(ignorePath));
        continue;
      }

      // Handle the rule en-/disable flags
      std::istringstream ss(currentString);
      std::string token;
      while (std::getline(ss, token, ',')) {
        DiagnosticsEngine::Level diagLevel;
        if (token.find("--") == 0) {
          token.erase(0, 2);
          diagLevel = DiagnosticsEngine::Remark;
        } else if (token.find('-') == 0) {
          token.erase(0, 1);
          diagLevel = DiagnosticsEngine::Warning;
        } else {
          diagLevel = DiagnosticsEngine::Error;
        }
        if (token == "all") {
          for (const auto &checkerName : getRegisteredCheckerNames()) {
            if (enableChecker(checkerName, diagLevel) == false) {
              assert(false &&
                     "Registered checkers have to be enabled successfully.");
            }
          }
        } else if (enableChecker(token, diagLevel) == false) {
          llvm::errs() << "Unknown checker: " << token << "\n";
          dumpRegisteredCheckers(llvm::errs());
          return false;
        }
      }
    }
    return true;
  }

  void PrintHelp(llvm::raw_ostream &ros) {
    ros << "Available plugin parameters:\n";
    ros << "[--help] - show this text\n";
    ros << "[--exclude-path=PATH] - do not check files matching PATH\n";
    ros << "[all|-all|--all] - report all rule violations as "
           "error/warning/remark\n";
    ros << "[RULE|-RULE|--RULE] - report rule RULE violations as "
           "error/warning/remark\n";
  }
};

static FrontendPluginRegistry::Add<Action> X("misra.cpp.2008",
                                             "MISRA C++ 2008");

void RuleCheckerASTContext::setContext(ASTContext &context) {
  this->context = &context;
}

RuleCheckerASTContext::RuleCheckerASTContext()
    : RuleChecker(), context(nullptr) {}

std::string RuleCheckerASTContext::srcLocToString(const SourceLocation start) {
  const clang::SourceManager &sm = context->getSourceManager();
  const clang::LangOptions lopt = context->getLangOpts();
  const SourceLocation spellingLoc = sm.getSpellingLoc(start);
  unsigned tokenLength =
      clang::Lexer::MeasureTokenLength(spellingLoc, sm, lopt);
  return std::string(sm.getCharacterData(spellingLoc),
                     sm.getCharacterData(spellingLoc) + tokenLength);
}

bool RuleCheckerASTContext::isInMainFile(const clang::SourceLocation loc) {
  const clang::SourceManager &sm = context->getSourceManager();
  const clang::FullSourceLoc fullSrcLoc = FullSourceLoc(loc, sm);
  return (fullSrcLoc.isValid() == false) ||
         (fullSrcLoc.getFileID() == sm.getMainFileID());
}

void RuleCheckerASTContext::doWork() {
  assert(context && "The context has to be set before calling this function.");
  assert(CI);
}

std::set<std::string> &getRegisteredCheckerNames() {
  static std::set<std::string> registeredCheckerNames;

  return registeredCheckerNames;
}

template <typename Registry>
/// \brief Pick up all registered checker names and put them into a set for
/// easier access later on.
class RCR : public Registry::listener {
private:
  virtual void registered(const typename Registry::entry &e) override {
    auto &registeredCheckerNames = getRegisteredCheckerNames();
    std::string n = Registry::traits::nameof(e);
    registeredCheckerNames.insert(n);
  }

public:
  RCR() { Registry::listener::init(); }
};

RCR<RuleCheckerASTContextRegistry> astRuleCheckerListener;
RCR<RuleCheckerPreprocessorRegistry> ppRuleCheckerListener;
}
