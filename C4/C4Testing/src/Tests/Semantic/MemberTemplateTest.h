#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// The members of template instances and of classes that declare templates: member function templates
	// (BUG-117), an instance's own static data members (BUG-115) and its members' emitted symbols (BUG-116).
	CLASS(GROUP = Testing) MemberTemplateTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::MemberTemplateTest);

		TEST_FUNC(Semantic.Templates.Members)
		void MemberTemplateIsAMethodOfItsOwner();
		TEST_FUNC(Semantic.Templates.Members)
		void QualifiedCallFindsMemberTemplates();
		TEST_FUNC(Semantic.Templates.Members)
		void ClassTemplateMemberTemplateSeesClassParameters();
		TEST_FUNC(Semantic.Templates.Members)
		void OutOfLineDefinitionsAttach();
		TEST_FUNC(Semantic.Templates.Members)
		void WrittenArgumentsNameOnlyTemplates();
		TEST_FUNC(Semantic.Templates.Members)
		void InstanceMembersHaveTheirOwnSymbols();
		TEST_FUNC(Semantic.Templates.Members)
		void InstanceStaticMembersAreTheirOwnObjects();
	};
} // namespace Testing
