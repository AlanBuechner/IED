#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"
#include "Model/QueryResult.h"

namespace Testing
{
	// The Phase 1 gate / Phase 2 oracle (07-migration.md): a battery of real programs where the
	// ProgramModel's answers are asserted against HAND-COMPUTED ground truth (known sizes, values,
	// method names), not by re-reading the same field. This proves the model is correct across the
	// cases Phase 2 will switch HIR over to - namespaces, templates, inheritance, operators, enums,
	// aliases, layout - before anything reads it in the live pipeline. A regression here stops the cutover.
	CLASS(GROUP = Testing) ModelParityTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ModelParityTest);

		TEST_FUNC(Semantic.ModelParity)
		void TypesAndLayoutMatchHandComputed();

		TEST_FUNC(Semantic.ModelParity)
		void QualifiedNamesResolveToConcreteTypes();

		TEST_FUNC(Semantic.ModelParity)
		void InheritanceSizeIsBasePlusMembers();

		TEST_FUNC(Semantic.ModelParity)
		void AliasIsTransparentToSameInstance();

		TEST_FUNC(Semantic.ModelParity)
		void ConstantsAndEnumsFoldToKnownValues();

		TEST_FUNC(Semantic.ModelParity)
		void OperatorsDispatchToTheDeclaringSide();

		TEST_FUNC(Semantic.ModelParity)
		void TemplateInstantiationHasConcreteLayout();

		TEST_FUNC(Semantic.ModelParity)
		void ForwardDeclaredTypeIsIncompleteButUsable();

		TEST_FUNC(Semantic.ModelParity)
		void OpenNamespaceBlocksMergeWithoutConflict();

		TEST_FUNC(Semantic.ModelParity)
		void ScopedAliasInTemplateSubstitutesToConcreteMember();

		TEST_FUNC(Semantic.ModelParity)
		void NonTypeParameterFoldsIntoArraySize();

		TEST_FUNC(Semantic.ModelParity)
		void AbstractTypeIsMarkedAbstract();

		TEST_FUNC(Semantic.ModelParity)
		void ResolveOverloadPicksBestAndReportsErrors();
	};
} // namespace Testing
