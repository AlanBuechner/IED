#include "Model/OperatorAnnotations.h"
#include "Model/OperatorTable.h" // CanonicalAnnotation: every spelling unifies to one canonical name
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"
#include "Model/TypeHandle.h"

namespace Scripting
{
	void CollectOperatorAnnotations(const AnnotationNode* ann, uint32 parameterCount, Core::Vector<Core::String>& out)
	{
		if (!ann) return;
		// Each name is normalized: [operator("[]")] and [operator("index")] both become "index".
		for (const auto& group : ann->GetBracketGroups())
			for (const auto& item : group.annotations)
				if (item && item->GetName() == "operator")
					for (const auto& arg : item->GetArguments())
						if (const auto* lit = dynamic_cast<const LiteralNode*>(arg.Get()))
							out.Push(OperatorTable::CanonicalAnnotation(lit->GetValue(), parameterCount));
	}

	QueryResult<void> CheckLifecycleOperators(const FunctionRecord& rec)
	{
		auto fail = [&](const Core::String& why) { return QueryResult<void>::Fail(QueryError::NotAValue, "[operator] on '" + rec.name + "': " + why); };
		const uint32 params = rec.type ? rec.type->GetParameterCount() : 0;
		for (const Core::String& a : rec.operatorAnnotations)
		{
			if (a == "destroy" || a == "finalizer")
				return fail("\"" + a + "\" is not a destructor; the destructor is [operator(\"delete\")]");
			if (a == "move")
				return fail("C4 has no move operator: objects relocate; remove [operator(\"move\")] (take(x) moves a value out of a variable)");
			if (a == "delete" && rec.containingType && params != 0)
				return fail("[operator(\"delete\")] takes no parameters");
			if (a == "copy" && rec.containingType)
			{
				const Type* p = params == 1 ? TypeHandle::UnwrapToValue(rec.type->GetParameters()[0].m_Type) : nullptr;
				if (p != rec.containingType)
					return fail("[operator(\"copy\")] takes one 'const " + rec.containingType->GetName() + "&' (it copies into a new object)");
			}
		}
		return QueryResult<void>::Success();
	}

	void CollectGroupAnnotations(const AnnotationNode* ann, Core::Vector<Core::String>& out)
	{
		if (!ann) return;
		for (const auto& group : ann->GetBracketGroups())
			for (const auto& item : group.annotations)
				if (item && item->GetName() == "group" && !item->GetArguments().Empty())
					if (const auto* lit = dynamic_cast<const LiteralNode*>(item->GetArguments()[0].Get()))
						out.Push(lit->GetValue()); // [group("components")] -> components
	}
} // namespace Scripting
