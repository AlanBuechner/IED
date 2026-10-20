#include "Model/TestRegistry.h"
#include "Model/SymbolTable.h"
#include "Model/TypeHandle.h"
#include "Model/Types/FunctionType.h"
#include "Model/Types/ObjectType.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"

namespace Scripting
{
	namespace
	{
		using Result = QueryResult<void>;

		// The harness calls a test or fixture with nothing: no receiver, no arguments, no use of a result.
		Result CallableAlone(const FunctionRecord& function, const Core::String& kind)
		{
			const Core::String what = kind == "test" ? "a test" : "a [" + kind + "]";
			const FunctionType* signature = function.type;
			if ((function.containingType || function.primitiveReceiverType) && !function.isStatic)
				return Result::Fail(QueryError::NotAValue, "test methods must be static: the harness calls '" + function.name +
					"' without an object; write 'public static'");
			if (signature && signature->GetParameterCount() > 0)
				return Result::Fail(QueryError::NotAValue, what + " takes no parameters, but '" + function.name + "' takes " +
					std::to_string(signature->GetParameterCount()));
			const Type* returned = signature ? TypeHandle::UnwrapToValue(signature->GetReturnType()) : nullptr;
			if (returned && returned->GetKind() != TypeKind::Void)
				return Result::Fail(QueryError::NotAValue, what + " returns nothing, but '" + function.name + "' returns '" + returned->GetName() + "'");
			return Result::Success();
		}

		Core::String Joined(const Core::String& prefix, const Core::String& name)
		{
			if (prefix.empty()) return name;
			return name.empty() ? prefix : prefix + "." + name;
		}
	}

	void TestRegistry::SetClassGroup(const ObjectType* owner, const Core::String& group)
	{
		m_ClassGroups[owner] = group;
	}

	TestRegistry::Annotation TestRegistry::Find(const Core::Vector<Ref<AnnotationNode>>& annotations)
	{
		Annotation found;
		for (const auto& annotation : annotations)
		{
			if (!annotation) continue;
			for (const auto& group : annotation->GetBracketGroups())
				for (const auto& item : group.annotations)
				{
					if (!item) continue;
					const Core::String& name = item->GetName();
					if (name != "test" && name != "test_setup" && name != "test_teardown") continue;
					found.kind = name;
					found.group.clear();
					if (!item->GetArguments().Empty())
						if (const auto* literal = dynamic_cast<const LiteralNode*>(item->GetArguments()[0].Get()))
							found.group = literal->GetValue();
				}
		}
		return found;
	}

	QueryResult<void> TestRegistry::Collect(const FunctionDefinitionNode& declaration, const FunctionRecord& function, uint32 unitId)
	{
		const Annotation annotation = Find(declaration.GetAnnotations());
		if (annotation.kind.empty()) return Result::Success();
		if (auto alone = CallableAlone(function, annotation.kind); !alone.Ok()) return alone;

		const ObjectType* owner = function.containingType;
		if (annotation.kind == "test")
		{
			Test test;
			test.function = &function;
			test.name = NameOf(function, annotation.group);
			test.owner = owner;
			test.sourceId = unitId;
			test.line = declaration.GetName() ? declaration.GetName()->GetFirstIdentifierToken().GetStartLocation().line : declaration.startLocation.line;
			m_Tests.push_back(test);
			return Result::Success();
		}
		Fixtures& fixtures = owner ? m_ClassFixtures[owner] : m_Global;
		return SetFixture(annotation.kind == "test_setup" ? fixtures.setup : fixtures.teardown, function, annotation.kind);
	}

	const TestRegistry::Fixtures& TestRegistry::ClassFixtures(const ObjectType* owner) const
	{
		static const Fixtures kNone;
		auto it = m_ClassFixtures.find(owner);
		return it != m_ClassFixtures.end() ? it->second : kNone;
	}

	// A method's test is named under its class's `[test("...")]`; an extension method is not in the class's body.
	Core::String TestRegistry::NameOf(const FunctionRecord& function, const Core::String& group) const
	{
		const ObjectType* owner = function.containingType;
		auto classGroup = owner && !function.isExtensionMethod ? m_ClassGroups.find(owner) : m_ClassGroups.end();
		if (classGroup != m_ClassGroups.end()) return Joined(classGroup->second, group.empty() ? function.name : group);
		if (!group.empty()) return group;
		return owner ? owner->GetName() + "." + function.name : function.name;
	}

	QueryResult<void> TestRegistry::SetFixture(const FunctionRecord*& slot, const FunctionRecord& function, const Core::String& kind)
	{
		if (slot && slot != &function)
			return Result::Fail(QueryError::NotAValue, "'" + function.name + "' is a second [" + kind + "] here: '" + slot->name +
				"' is already one");
		slot = &function;
		return Result::Success();
	}
} // namespace Scripting
