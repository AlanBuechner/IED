#pragma once
#include "Core/Core.h"
#include "Model/QueryResult.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include <unordered_map>
#include <vector>

namespace Scripting
{
	struct FunctionDefinitionNode;
	struct FunctionRecord;
	class ObjectType;

	/// The program's tests (docs/runtime/03): every `[test]`, `[test_setup]` and `[test_teardown]` declaration,
	/// collected once by the model builder and read by the test harness, so the two cannot disagree.
	class TestRegistry
	{
	public:
		struct Test
		{
			const FunctionRecord* function = nullptr;
			Core::String name;                 // the group path (`math.vector.addition`), or `Type.Function` / `Function`
			const ObjectType* owner = nullptr; // the class whose fixtures bracket it; null for a free test
			uint32 sourceId = 0;
			uint32 line = 0;
		};
		struct Fixtures
		{
			const FunctionRecord* setup = nullptr;
			const FunctionRecord* teardown = nullptr;
		};

		/// A class's own `[test("a.b")]`: the group its methods' tests are named under.
		void SetClassGroup(const ObjectType* owner, const Core::String& group);

		/// The test annotation on one function declaration, if it has one, or why that function cannot be a test.
		QueryResult<void> Collect(const FunctionDefinitionNode& declaration, const FunctionRecord& function, uint32 unitId);

		const std::vector<Test>& Tests() const { return m_Tests; }
		bool Empty() const { return m_Tests.empty(); }
		const Fixtures& GlobalFixtures() const { return m_Global; }
		/// The fixtures of `owner`'s tests; an empty set when the class has none.
		const Fixtures& ClassFixtures(const ObjectType* owner) const;

		/// The `[test("group")]` (or `[test_setup]` / `[test_teardown]`) on a declaration: its kind and group.
		struct Annotation
		{
			Core::String kind; // "test", "test_setup" or "test_teardown"; empty when there is none
			Core::String group;
		};
		static Annotation Find(const Core::Vector<Ref<AnnotationNode>>& annotations);

	private:
		Core::String NameOf(const FunctionRecord& function, const Core::String& group) const;
		QueryResult<void> SetFixture(const FunctionRecord*& slot, const FunctionRecord& function, const Core::String& kind);

		std::vector<Test> m_Tests;
		Fixtures m_Global;
		std::unordered_map<const ObjectType*, Fixtures> m_ClassFixtures;
		std::unordered_map<const ObjectType*, Core::String> m_ClassGroups;
	};
} // namespace Scripting
