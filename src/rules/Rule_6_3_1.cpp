//===-  Rule_6_3_1.cpp - Checker for MISRA C++ 2008 rule 6-3-1-------------===//
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

class Rule_6_3_1 : public RuleCheckerASTContext,
                   public RecursiveASTVisitor<Rule_6_3_1> {
public:
  Rule_6_3_1() : RuleCheckerASTContext() {}

  bool VisitStmt(Stmt *S) {
    if (doIgnore(S->getLocStart())) {
      return true;
    }

    if (const auto *fs = dyn_cast<ForStmt>(S)) {
      ReportIfNotCompound(fs->getBody());
    } else if (const auto *ds = dyn_cast<DoStmt>(S)) {
      ReportIfNotCompound(ds->getBody());
    } else if (const auto *ws = dyn_cast<WhileStmt>(S)) {
      ReportIfNotCompound(ws->getBody());
    }
    return true;
  }

private:
  void ReportIfNotCompound(const Stmt *S) {
    if (!isa<CompoundStmt>(S)) {
      reportError(S->getLocStart());
    }
  }

protected:
  virtual void doWork() override {
    RuleCheckerASTContext::doWork();
    this->TraverseDecl(context->getTranslationUnitDecl());
  }
};

static RuleCheckerASTContextRegistry::Add<Rule_6_3_1> X("6-3-1", "");
}
