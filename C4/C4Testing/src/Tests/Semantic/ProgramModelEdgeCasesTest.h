#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"
#include "Model/SymbolTable.h"
#include "Model/QueryResult.h"
#include "Model/TypeHandle.h" // UnwrapToValue brick (moved off the Type base)

namespace Testing
{
	// ProgramModel edge cases: the hard combinations of language features that a lower system will
	// rely on - templates (nested, alias, non-type, operators on instantiations, function templates),
	// namespaces (shadowing, open-namespace merge, cross-unit out-of-line NS::Type::Func()), aliases
	// (chains, pointer-to-alias), overloading (defaults, ambiguity), inheritance/polymorphism
	// (virtuals, vptr layout), the [test(a.b)] registry + class fixtures, and extension methods
	// (namespaced targets, targets that are template instantiations). Each test asserts the CONCRETE
	// facts a lower system needs: resolved types, layouts, overload picks, operator bindings, records.
	CLASS(GROUP = Testing) ProgramModelEdgeCasesTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ProgramModelEdgeCasesTest);

		// --- Templates -------------------------------------------------------------
		TEST_FUNC(Semantic.ModelEdgeCases)
		void NestedTemplateInstantiationIsConcrete();

		TEST_FUNC(Semantic.ModelEdgeCases)
		void AliasTemplateYieldsSameInstantiation();

		TEST_FUNC(Semantic.ModelEdgeCases)
		void NonTypeParameterDrivesArrayLayout();

		TEST_FUNC(Semantic.ModelEdgeCases)
		void OperatorOnInstantiatedTypeResolves();

		TEST_FUNC(Semantic.ModelEdgeCases)
		void FunctionTemplateExplicitArgsInstantiateAndCache();

		// --- Namespaces ------------------------------------------------------------
		TEST_FUNC(Semantic.ModelEdgeCases)
		void SameNameInTwoNamespacesIsAmbiguousUnqualified();

		TEST_FUNC(Semantic.ModelEdgeCases)
		void OpenNamespaceBlocksMergeWithoutConflict();

		TEST_FUNC(Semantic.ModelEdgeCases)
		void CrossUnitOutOfLineQualifiedDefinition();

		// --- Aliases ---------------------------------------------------------------
		TEST_FUNC(Semantic.ModelEdgeCases)
		void AliasChainResolvesToSameObject();

		TEST_FUNC(Semantic.ModelEdgeCases)
		void PointerToAliasUnwrapsForMemberAccess();

		// --- Overloading -----------------------------------------------------------
		TEST_FUNC(Semantic.ModelEdgeCases)
		void DefaultArgumentsFillAtResolution();

		// --- Inheritance / polymorphism --------------------------------------------
		TEST_FUNC(Semantic.ModelEdgeCases)
		void VirtualMethodIsInheritedAndOverridden();

		TEST_FUNC(Semantic.ModelEdgeCases)
		void PolymorphicHierarchyHasSingleVptr();

		// --- Test registry ---------------------------------------------------------
		TEST_FUNC(Semantic.ModelEdgeCases)
		void TestRegistryCollectsGroupsAndSymbols();

		TEST_FUNC(Semantic.ModelEdgeCases)
		void ClassFixturesFromStaticSetupTeardown();
		TEST_FUNC(Semantic.ModelEdgeCases)
		void ClassTestGroupPrefixesItsMethods();

		// --- Extension methods -----------------------------------------------------
		TEST_FUNC(Semantic.ModelEdgeCases)
		void ExtensionMethodOnNamespacedClassIsFound();

		TEST_FUNC(Semantic.ModelEdgeCases)
		void ExtensionMethodOnInstantiatedTypeIsFound();

		// --- Combined stress: every concept at once --------------------------------
		TEST_FUNC(Semantic.ModelEdgeCases)
		void CombinedNamespaceTemplateInheritOperatorExtension();

		// --- Built-in aggregate members (init list) ------------------------------
		TEST_FUNC(Semantic.ModelEdgeCases)
		void InitListTypeExposesDataAndSizeMembers();

		// --- Anonymous union / swizzle ------------------------------------------
		TEST_FUNC(Semantic.ModelEdgeCases)
		void AnonymousUnionLanesFlattenIntoParentWithSharedLayout();

		// --- Operator canonicalization (the catalog matches the language spec) ----
		TEST_FUNC(Semantic.ModelEdgeCases)
		void OperatorAnnotationsNormalizeToSpecCanonicalNames();
	};
} // namespace Testing
