//===-  Rule_18_0_3.cpp - Checker for MISRA C++ 2008 rule 18-0-3-----------===//
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "misracpp2008.h"
#include <set>
#include <string>

using namespace clang;

namespace misracpp2008 {

class Rule_18_0_3 : public RuleCheckerASTContext,
                    public RecursiveASTVisitor<Rule_18_0_3> {
private:
  static const std::set<std::string> illegalFunctions;

public:
  bool VisitDeclRefExpr(DeclRefExpr *expr) {
    if (doIgnore(expr->getLocation())) {
      return true;
    }

    std::string funName = expr->getNameInfo().getAsString();
    if (illegalFunctions.count(funName)) {
      reportError(expr->getLocStart());
    }
    return true;
  }

protected:
  virtual void doWork() override {
    RuleCheckerASTContext::doWork();
    this->TraverseDecl(context->getTranslationUnitDecl());
  }
};

const std::set<std::string> Rule_18_0_3::illegalFunctions = {
  "abort", "exit", "getenv", "system"
};

static RuleCheckerASTContextRegistry::Add<Rule_18_0_3> X("18-0-3", "");
}
