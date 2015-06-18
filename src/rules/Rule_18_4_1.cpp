//===-  Rule_18_4_1.cpp - Checker for MISRA C++ 2008 rule 18-4-1-----------===//
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "misracpp2008.h"

using namespace clang;

namespace misracpp2008 {

class Rule_18_4_1 : public RuleCheckerASTContext,
                    public RecursiveASTVisitor<Rule_18_4_1> {
public:
  Rule_18_4_1() : RuleCheckerASTContext() {}
  bool VisitCXXNewExpr(CXXNewExpr *decl) {
    if (doIgnore(decl->getStartLoc())) {
      return true;
    }

    // Print error on all new usages except for the placement news
    if (decl->getNumPlacementArgs() == 0 ||
        decl->shouldNullCheckAllocation(*context)) {
      reportError(decl->getLocStart());
    }

    return true;
  }

protected:
  virtual void doWork() override {
    RuleCheckerASTContext::doWork();
    this->TraverseDecl(context->getTranslationUnitDecl());
  }
};

static RuleCheckerASTContextRegistry::Add<Rule_18_4_1> X("18-4-1", "");
}
