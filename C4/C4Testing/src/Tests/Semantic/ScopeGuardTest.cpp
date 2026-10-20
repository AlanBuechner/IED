#include "ScopeGuardTest.h"
#include "Model/SymbolTable.h"

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		SymbolInfo Variable()
		{
			SymbolInfo info;
			info.kind = SymbolKind::Variable;
			return info;
		}
	}

	void ScopeGuardTest::APushedLayerIsCurrentUntilItsGuardEnds()
	{
		SymbolTable table;
		Scope* outer = table.Current();
		const uint32 children = outer->GetChildren().Count();
		{
			ScopeGuard guard = table.PushScope(Scope::Kind::Block, "body");
			TEST_ASSERT(guard.Valid());
			TEST_ASSERT(table.Current() == guard.GetScope());
			TEST_ASSERT(guard.GetScope()->GetParent() == outer);
			TEST_ASSERT(guard.GetScope()->GetKind() == Scope::Kind::Block);
			TEST_ASSERT_EQ(outer->GetChildren().Count(), children + 1);
		}
		TEST_ASSERT(table.Current() == outer);
		TEST_ASSERT_EQ(outer->GetChildren().Count(), children);
		TEST_PASSED();
	}

	void ScopeGuardTest::NestedGuardsUnwindInOrder()
	{
		SymbolTable table;
		Scope* outer = table.Current();
		{
			ScopeGuard function = table.PushScope(Scope::Kind::Function, "F");
			{
				ScopeGuard block = table.PushScope(Scope::Kind::Block, "if");
				TEST_ASSERT(block.GetScope()->GetParent() == function.GetScope());
				TEST_ASSERT(table.Current() == block.GetScope());
			}
			TEST_ASSERT(table.Current() == function.GetScope()); // the inner guard ends only its own layer
			TEST_ASSERT_EQ(function.GetScope()->GetChildren().Count(), 0u);
		}
		TEST_ASSERT(table.Current() == outer);
		TEST_PASSED();
	}

	void ScopeGuardTest::ANameInALayerLeavesWithIt()
	{
		SymbolTable table;
		Scope* outer = table.Current();
		{
			ScopeGuard guard = table.PushScope(Scope::Kind::Block, "body");
			TEST_ASSERT(guard.GetScope()->Register("local", Variable()));
			TEST_ASSERT(table.Current()->Find("local") != nullptr);
		}
		TEST_ASSERT(outer->Find("local") == nullptr);
		ScopeGuard again = table.PushScope(Scope::Kind::Block, "body");
		TEST_ASSERT(again.GetScope()->Find("local") == nullptr); // a new layer starts empty
		TEST_PASSED();
	}

	void ScopeGuardTest::ALayerSeesItsParentsNames()
	{
		SymbolTable table;
		TEST_ASSERT(table.Current()->Register("global", Variable()));
		ScopeGuard function = table.PushScope(Scope::Kind::Function, "F");
		ScopeGuard block = table.PushScope(Scope::Kind::Block, "loop");
		TEST_ASSERT(block.GetScope()->Find("global") != nullptr);
		TEST_ASSERT(block.GetScope()->Register("global", Variable())); // shadows it in this layer only
		TEST_ASSERT(block.GetScope()->Find("global") != function.GetScope()->Find("global"));
		TEST_PASSED();
	}
} // namespace Testing
