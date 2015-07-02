//===-  Rule_6_2_2.cpp - Checker for MISRA C++ 2008 rule 6-2-2-------------===//
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/APFloat.h"
#include "misracpp2008.h"
#include <algorithm>

using namespace clang;

namespace misracpp2008 {

///
/// \brief The Rule_6_2_2 class
/// \note My guts say that this implementation is more complicated than it needs
/// to be. My brain however can not think of a simple version. If you read this,
/// feel free prove your superiority and improve this.
class Rule_6_2_2 : public RuleCheckerASTContext,
                   public RecursiveASTVisitor<Rule_6_2_2> {
public:
  Rule_6_2_2() : RuleCheckerASTContext() {}

  bool VisitBinEQ(BinaryOperator *S) {
    reportViolationIfFloatSubexpr(S);
    return true;
  }
  bool VisitBinNE(BinaryOperator *S) {
    reportViolationIfFloatSubexpr(S);
    return true;
  }

  bool VisitBinLAnd(BinaryOperator *S) {
    FindObfuscatedViolation(S);
    return true;
  }
  bool VisitBinLOr(BinaryOperator *S) {
    FindObfuscatedViolation(S);
    return true;
  }

private:
  /// \brief Helper class to describe the source of a floating value.
  struct FloatEmiter {
  private:
    union {
      double constDblVal;
      const Decl *decl;
      size_t arrayIndex;
    } data;
    const enum Type {
      TYPE_DOUBLE,    ///< Compile time double value is provided.
      TYPE_DECL,      ///< Pointer to a declaration is provided.
      TYPE_ARRAY_SUB, ///< Pointer to an array declaration and the index into is
                      ///  provided.
      TYPE_RUNTIME,   ///< Value can not be compile-time-determined at all.
    } type;

    FloatEmiter(Type type) : type(type) {}

  public:
    FloatEmiter() : type(TYPE_RUNTIME) {}
    FloatEmiter(const double &dblValue) : FloatEmiter(TYPE_DOUBLE) {
      data.constDblVal = dblValue;
    }
    FloatEmiter(const Decl *decl) : FloatEmiter(TYPE_DECL) {
      this->data.decl = decl;
    }
    FloatEmiter(const Decl *decl, size_t arrayIndex)
        : FloatEmiter(TYPE_ARRAY_SUB) {
      this->data.decl = decl;
      this->data.arrayIndex = arrayIndex;
    }
    bool operator==(const FloatEmiter &other) const {
      if (type != other.type) {
        return false;
      }
      switch (type) {
      case TYPE_DOUBLE:
        return data.constDblVal == other.data.constDblVal;
        break;
      case TYPE_DECL:
        return data.decl == other.data.decl;
        break;
      case TYPE_ARRAY_SUB:
        return data.decl == other.data.decl &&
               data.arrayIndex == other.data.arrayIndex;
        break;
      case TYPE_RUNTIME:
        return this == &other;
        break;
      }
      assert(false && "The switch above should handle all cases.");
      // Fallback
      return this == &other;
    }
    /// Implemented to make \ref FloatEmiter usable in a set.
    bool operator<(const FloatEmiter &other) const {
      if (type != other.type) {
        return type < other.type;
      }
      switch (type) {
      case TYPE_DOUBLE:
        return data.constDblVal < other.data.constDblVal;
        break;
      case TYPE_DECL:
        return data.decl < other.data.decl;
        break;
      case TYPE_ARRAY_SUB:
        if (data.decl < other.data.decl) {
          return true;
        }
        return data.arrayIndex < other.data.arrayIndex;
        break;
      case TYPE_RUNTIME:
        return this < &other;
        break;
      }
      assert(false && "The switch above should handle all cases.");
      // Fallback
      return this < &other;
    }
  };
  using FloatEmiterSet = std::set<FloatEmiter>;

  bool extractAPInt(const Expr *expr, llvm::APInt &aPInt) const {
    Expr::EvalResult evalResult;
    if (expr->EvaluateAsRValue(evalResult, *this->context)) {
      if (evalResult.Val.isInt()) {
        aPInt = evalResult.Val.getInt();
        return true;
      }
    }
    return false;
  }

  bool extractConstInt(const Expr *expr, int64_t &sizeValue) const {
    llvm::APInt i;
    if (extractAPInt(expr, i)) {
      sizeValue = i.getSExtValue();
      return true;
    }
    return false;
  }

  bool extractConstUnsignedInt(const Expr *expr, uint64_t &sizeValue) const {
    llvm::APInt i;
    if (extractAPInt(expr, i) && i.isNonNegative()) {
      sizeValue = i.getLimitedValue();
      return true;
    }
    return false;
  }

  static bool isFloatingTypeExpr(const Expr *expr) {
    return expr->IgnoreParenCasts()->getType()->isFloatingType();
  }

  /// Figure out if the left or the right type looks like a floating type.
  /// \note If one of them is of floating type, the other one must be so too, or
  /// gets casted to.
  static bool isBinOpComparingFloatings(const BinaryOperator *bo) {
    return isFloatingTypeExpr(bo->getLHS()) || isFloatingTypeExpr(bo->getRHS());
  }

  /// Report not-so-obvious cases like "if (a <= b && a >= b) {}"
  void FindObfuscatedViolation(BinaryOperator *binOp) {
    assert((binOp->getOpcode() == BO_LAnd || binOp->getOpcode() == BO_LOr) &&
           "This method only works on logical operators.");

    if (doIgnore(binOp->getLocStart())) {
      return;
    }

    // There must be binary operators on both sides. Bail out otherwise.
    const auto *lhsBinOp =
        dyn_cast<BinaryOperator>(binOp->getLHS()->IgnoreParenCasts());
    const auto *rhsBinOp =
        dyn_cast<BinaryOperator>(binOp->getRHS()->IgnoreParenCasts());
    if (!(lhsBinOp && rhsBinOp && isBinOpComparingFloatings(lhsBinOp) &&
          isBinOpComparingFloatings(rhsBinOp))) {
      return;
    }

    const FloatEmiterSet lhsChildren = getChildrenFloatEmiters(lhsBinOp);
    const FloatEmiterSet rhsChildren = getChildrenFloatEmiters(rhsBinOp);

    // Report error only when on the left and on the right side the same two
    // values (constants, method calls, etc.) have been found.
    if (std::equal(lhsChildren.begin(), lhsChildren.end(),
                   rhsChildren.begin())) {
      reportError(binOp->getLocStart());
    }
  }

  /// \brief Report if the binary operator \c binOp compares floatings.
  /// \param binOp BinaryOperator, either BinaryOperatorKind::BO_EQ or
  /// BinaryOperatorKind::BO_NE
  /// \return True if a violation has been found, false otherwise.
  bool reportViolationIfFloatSubexpr(BinaryOperator *binOp) {
    assert((binOp->getOpcode() == BO_EQ || binOp->getOpcode() == BO_NE) &&
           "This method only works on (not) equal operators.");

    if (doIgnore(binOp->getLocStart())) {
      return false;
    }

    // Return when an issue has been found. Report no more than one issue.
    if (isFloatingTypeExpr(binOp->getLHS())) {
      reportError(binOp->getLHS()->getLocStart());
      return true;
    } else if (isFloatingTypeExpr(binOp->getRHS())) {
      reportError(binOp->getRHS()->getLocStart());
      return true;
    }

    // No violation found
    return false;
  }

  const FloatEmiterSet getChildrenFloatEmiters(const BinaryOperator *bo) {
    return FloatEmiterSet(
        {extractFloatEmiter(bo->getRHS()), extractFloatEmiter(bo->getLHS())});
  }

  bool extractConstDouble(const Expr *expr, double &dblValue) const {
    Expr::EvalResult evalResult;
    if (expr->EvaluateAsRValue(evalResult, *this->context)) {
      if (evalResult.Val.isFloat()) {
        using namespace llvm;
        APFloat APF = evalResult.Val.getFloat();
        bool unused;
        APF.convert(APFloat::IEEEdouble, APFloat::rmNearestTiesToEven, &unused);
        dblValue = APF.convertToDouble();
        return true;
      }
    }
    return false;
  }

  /// Trying to describe/classify an expression which emits a float value.
  FloatEmiter extractFloatEmiter(const Expr *expr) const {
    const Expr *coreExpr = expr->IgnoreParenImpCasts();

    // Check if this value can be evaluated at compile time.
    double dblValue;
    int64_t intValue;
    if (extractConstDouble(coreExpr, dblValue)) {
      return FloatEmiter(dblValue);
    }
    if (extractConstInt(coreExpr, intValue)) {
      return FloatEmiter(intValue);
    }

    // It is not a constant value. Lets try to figure out the declaration which
    // refers to the value.
    if (const auto *declRef = dyn_cast<DeclRefExpr>(coreExpr)) {
      return FloatEmiter(declRef->getDecl());
    }
    if (const auto *arraySub = dyn_cast<ArraySubscriptExpr>(coreExpr)) {
      const auto *decl =
          dyn_cast<DeclRefExpr>(arraySub->getBase()->IgnoreParenImpCasts());
      if (decl == nullptr) {
        arraySub->dump();
      }
      assert(decl &&
             "Base of an ArraySubscriptExpr is expected to be a DeclRefExpr.");

      uint64_t arrayIndex;
      if (extractConstUnsignedInt(arraySub->getIdx(), arrayIndex)) {
        return FloatEmiter(decl->getDecl(), static_cast<size_t>(arrayIndex));
      }
      // Without knowing the index we can not be sure if the same index gets
      // referenced. :(
      return FloatEmiter();
    }

    expr->dumpColor();
    assert(false && "Unsupported kind of floating value source!");
    return FloatEmiter(0.0);
  }

protected:
  virtual void doWork() override {
    RuleCheckerASTContext::doWork();
    this->TraverseDecl(context->getTranslationUnitDecl());
  }
};

static RuleCheckerASTContextRegistry::Add<Rule_6_2_2> X("6-2-2", "");
}
