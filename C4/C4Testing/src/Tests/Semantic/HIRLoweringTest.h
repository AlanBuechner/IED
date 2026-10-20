#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"
#include "Backend/HIR/HIR.h"

namespace Testing
{
	// HIR LOWERING TESTS - drive the REAL lowerer (docs/compiler/03-hir.md). Each test builds a
	// program model from in-memory source, lowers every function body with LowerFunction, and
	// asserts the produced tree: slot counts, node kinds, lvalue shapes, and - for bad code - the
	// diagnostics. This is the "simulated lowering" gate: it proves the API is clean enough to lower
	// a real body (push/pop scopes, shadowing, member access, calls, operators) and that every fact
	// a downstream system needs is present. Local types/functions created on the current scope are a
	// model-API capability (covered by SimulatedLoweringTest); the grammar does not emit them in a body.
	CLASS(GROUP = Testing) HIRLoweringTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::HIRLoweringTest);

		// --- straight-line lowering (Phase 1 gate) ------------------------------------
		TEST_FUNC(Semantic.HIRLowering)
		void StraightLineFunctionLowersToTypedTree();

		TEST_FUNC(Semantic.HIRLowering)
		void LocalVariableBecomesSlotAndStore();

		TEST_FUNC(Semantic.HIRLowering)
		void ShadowedLocalUsesInnermostSlot();

		// --- expressions (Phase 2) -----------------------------------------------------
		TEST_FUNC(Semantic.HIRLowering)
		void MemberAccessOnThisProducesFieldLValue();

		TEST_FUNC(Semantic.HIRLowering)
		void FreeFunctionCallResolvesToRecord();

		TEST_FUNC(Semantic.HIRLowering)
		void PrimitiveOperatorIsIntrinsicNotCall();

		TEST_FUNC(Semantic.HIRLowering)
		void MethodOperatorOnObjectIsCall();

		// --- template bodies (04.4 fake aliases) --------------------------------------
		TEST_FUNC(Semantic.HIRLowering)
		void InstantiatedTemplateMethodLowersWithBoundType();

		// --- diagnostics (06): HIR owns body validation --------------------------------
		TEST_FUNC(Semantic.HIRLowering)
		void UndefinedIdentifierIsReportedAndTreeIncomplete();

		TEST_FUNC(Semantic.HIRLowering)
		void ReturnMismatchIsReported();

		TEST_FUNC(Semantic.HIRLowering)
		void FailedInitializerIsReportedOnce();

		// --- control flow (05 statement rows) --------------------------------------------
		TEST_FUNC(Semantic.HIRLowering)
		void WhileLoopLowersToChildRegionWithLabels();

		TEST_FUNC(Semantic.HIRLowering)
		void BreakTargetsInnermostLoopNotOuter();

		TEST_FUNC(Semantic.HIRLowering)
		void ForLoopLowersInitCondStepBody();

		TEST_FUNC(Semantic.HIRLowering)
		void SwitchLowersFoldedCasesAndDefault();

		TEST_FUNC(Semantic.HIRLowering)
		void BreakInsideSwitchTargetsSwitchNotLoop();

		TEST_FUNC(Semantic.HIRLowering)
		void IfElseLowersBothBranchesAsOwnedRegions();

		TEST_FUNC(Semantic.HIRLowering)
		void LocalDeclaredInLoopScopeEndsWithLoop();

		TEST_FUNC(Semantic.HIRLowering)
		void DoWhileLowersWithIsDoWhileFlag();

		// --- complex situations (05 + 09): combined features in one body -----------------
		TEST_FUNC(Semantic.HIRLowering)
		void NestedLoopsWithShadowingAndBreak();

		TEST_FUNC(Semantic.HIRLowering)
		void ArrayIndexLowersToDerefOverAddressArithmetic();

		TEST_FUNC(Semantic.HIRLowering)
		void NewExpressionLowersToNewNodePointer();

		TEST_FUNC(Semantic.HIRLowering)
		void ControlFlowWithCallsAndOperators();

		// --- range-based for (desugared to an index loop) ----------------------------------
		TEST_FUNC(Semantic.HIRLowering)
		void RangeForOverArrayDesugarsToIndexLoop();

		TEST_FUNC(Semantic.HIRLowering)
		void RangeForVariableIsCopyNotReference();

		// Custom iterator protocol (begin/end/notequal/dereference/increment), as in the corpus.
		TEST_FUNC(Semantic.HIRLowering)
		void RangeForCustomIteratorProtocolDesugarsToOperatorCalls();

		// Braced aggregate initializers: declare + one Store per element.
		TEST_FUNC(Semantic.HIRLowering)
		void StructBracedInitStoresEachFieldPositionally();

		TEST_FUNC(Semantic.HIRLowering)
		void ArrayBracedInitStoresPositionalAndKeyedAndRange();

		// Types and functions declared INSIDE a body (doc 06), lowered by the real lowerer.
		TEST_FUNC(Semantic.HIRLowering)
		void LocalStructInBodyIsUsableWithinItsScope();

		TEST_FUNC(Semantic.HIRLowering)
		void LocalFunctionInBodyResolvesAndLowersNestedTree();

		// Exception handling: try/catch/finally + throw + rethrow.
		TEST_FUNC(Semantic.HIRLowering)
		void TryCatchFinallyLowersClausesAndThrow();

		// Braced list as a call argument (__init_list<T>): materializes into an InitListNode.
		TEST_FUNC(Semantic.HIRLowering)
		void InitListCallArgumentMaterializesElements();

		// Range-for over __init_list<T>: bounded by the list's own size, indexing its data pointer.
		TEST_FUNC(Semantic.HIRLowering)
		void RangeForOverInitListUsesDataAndSizeMembers();

		// Indexing an init list: l[i] reads through the list's data pointer member.
		TEST_FUNC(Semantic.HIRLowering)
		void InitListIndexLowersToDerefOverDataPointer();

		// Local decl of __init_list<T>: stores a materialized {ptr,size} aggregate into the slot.
		TEST_FUNC(Semantic.HIRLowering)
		void InitListLocalDeclStoresMaterializedAggregate();

		// Mixed-type arithmetic/compare converts operands to their common (usual-arithmetic) type.
		TEST_FUNC(Semantic.HIRLowering)
		void MixedTypeComparisonConvertsToCommonType();

		// Non-type template parameter used in a method body: N is a constant, not addressable storage.
		TEST_FUNC(Semantic.HIRLowering)
		void NonTypeParamInMethodBodyLowersAsConstant();

		// Comma declarator list (`int32 a = 1, b = a + 2;`): every name gets its own slot.
		TEST_FUNC(Semantic.HIRLowering)
		void CommaDeclaratorsEachGetOwnSlotAndType();

		// Short-circuit logical operators (&& / ||) are dedicated AST node types and lower to the
		// ternary region structure, not a call or an intrinsic.
		TEST_FUNC(Semantic.HIRLowering)
		void ShortCircuitLowersToTernaryStructure();

		// Compound assignment (s += v): operator method wins on objects; primitives use the plain
		// builtin op with mixed-type conversion, then store back.
		TEST_FUNC(Semantic.HIRLowering)
		void CompoundAssignmentLowersOpThenStore();

		// Extension methods carry their extends target as containingType so lowering establishes `this`.
		TEST_FUNC(Semantic.HIRLowering)
		void ExtensionMethodRecordCarriesContainingType();

		// sizeof(T) is a compile-time type query: it lowers to an int64 ConstantNode of T's byte size.
		TEST_FUNC(Semantic.HIRLowering)
		void SizeOfLowersToByteSizeConstant();

		// switch fallthrough (C semantics): a case that does not end in break/return jumps to the next
		// case's entry; an explicit `fallthrough;` does too. Both must lower to a JumpNode.
		TEST_FUNC(Semantic.HIRLowering)
		void SwitchFallthroughJumpsToNextCase();

		// A float literal with an f/F suffix is Float32, without it is Float64. This used to type every
		// float literal as double, so `float F() { return 0.0f; }` failed "got 'double', returns 'float'".
		TEST_FUNC(Semantic.HIRLowering)
		void FloatLiteralSuffixSelectsFloat32();

		// A namespace-qualified call `Ns::Fn(...)` parses its target as a StaticAccessNode. It must be
		// joined into "Ns::Fn" and resolved via QueryFunction/FindVisibleQualified - this used to hit
		// "call target is not a name or member" because ResolveCall only handled identifier/member callees.
		TEST_FUNC(Semantic.HIRLowering)
		void NamespaceQualifiedCallResolves();

		// A numeric return value is implicitly converted to the declared return type (widening and rank
		// changes: int32 -> int64, int32 -> uint32). This used to be a hard "return type mismatch" error.
		TEST_FUNC(Semantic.HIRLowering)
		void NumericReturnIsImplicitlyConverted();

		// A pointer is a first-class value type: int32* p is NOT int32. Deref (*p), member access through
		// a pointer (p->x), and delete p must all see the pointer, not its pointee. This used to fail with
		// "cannot dereference a non-pointer" because UnwrapToValue wrongly unwrapped pointers.
		TEST_FUNC(Semantic.HIRLowering)
		void PointerParamIsFirstClassValue();

		// delete p lowers to a DeleteNode (the symmetric counterpart of new). The operand must be a
		// pointer; deleting a non-pointer is a clean error. Codegen expands the node to [operator("delete")] + free.
		TEST_FUNC(Semantic.HIRLowering)
		void DeleteLowersToDeleteNode();

		// A function template with NON-TYPE parameters (template<int N>, template<T, uint32 I>) must
		// instantiate: each literal arg folds to a value and binds as a const in the binding scope, so the
		// body sees N/I as constants. This used to fail "cannot instantiate function template" because the
		// path only handled type args.
		TEST_FUNC(Semantic.HIRLowering)
		void FunctionTemplateNonTypeParamsInstantiate();

		// --- function pointers -------------------------------------------------------
		TEST_FUNC(Semantic.HIRLowering)
		void FunctionPointerInvocationLowersToIndirectCall();

		// --- null as a pointer/funcptr value -----------------------------------------
		TEST_FUNC(Semantic.HIRLowering)
		void NullLiteralCoercesIntoPointerAndFuncPtr();

		// --- function-to-pointer decay -------------------------------------------------
		TEST_FUNC(Semantic.HIRLowering)
		void FunctionNameDecaysToFunctionPointer();

		// --- safe cast (as) and defer --------------------------------------------------
		TEST_FUNC(Semantic.HIRLowering)
		void SafeCastNumericTargetIsRejected();
		TEST_FUNC(Semantic.HIRLowering)
		void SafeCastUpcastLowersToConvert();
		TEST_FUNC(Semantic.HIRLowering)
		void SafeCastDowncastReadsTypeInformation();
		TEST_FUNC(Semantic.HIRLowering)
		void SafeCastBetweenUnrelatedTypesIsRejected();
		TEST_FUNC(Semantic.HIRLowering)
		void DeferLowersToDeferNodesInSourceOrder();
		TEST_FUNC(Semantic.HIRLowering)
		void EmptyStatementLowersAsNoOp();
	};
} // namespace Testing
