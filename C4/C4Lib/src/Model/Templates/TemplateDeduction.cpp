#include "Model/Templates/TemplateDeduction.h"
#include "Model/Templates/PartialOrdering.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/FunPtrType.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/FunPtrTypeNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"

namespace Scripting
{
	namespace
	{
		using Result = QueryResult<void>;

		Result Nothing() { return Result::Success(); }

		const ObjectType* AsObject(const Type* t)
		{
			t = TypeHandle::UnwrapToValue(t);
			return t && t->GetKind() == TypeKind::Object ? static_cast<const ObjectType*>(t) : nullptr;
		}

		// The template parameter a pattern names when it is a plain name (`T`, `N`), else "".
		Core::String PlainName(const AstNode* pattern)
		{
			if (const auto* id = dynamic_cast<const IdentifierExpressionNode*>(pattern)) return id->GetName();
			const auto* q = dynamic_cast<const QualTypeNode*>(pattern);
			if (!q || q->IndirectionCount() || q->IsLeadingConst() || q->FuncPtrType() || !q->BaseName()) return "";
			const auto& segs = q->BaseName()->GetSegments();
			if (segs.Count() != 1 || segs[0].templateArgs) return "";
			return segs[0].nameToken.GetText();
		}

		TemplateArg ArgOf(const ObjectType::TemplateArgument& a)
		{
			TemplateArg arg;
			arg.type = a.type;
			if (!a.type) arg.value = a.value;
			return arg;
		}
	}

	TemplateDeduction::TemplateDeduction(const ProgramModel& model, const TemplateDefinitionNode& params, TemplateArgs seed)
		: m_Model(model), m_Params(params), m_PackIndex(PackIndex(params))
	{
		const uint32 fixed = FixedCount(params);
		for (uint32 i = 0; i < fixed; ++i) m_Args.Push(i < seed.Count() ? seed[i] : TemplateArg());
		for (const TemplateArg& a : m_Args) m_Written.push_back(a.IsSet());
		for (uint32 i = fixed; i < seed.Count(); ++i) m_Pack.Push(seed[i]);
		m_PackWritten = !m_Pack.Empty();
	}

	TemplateArgs TemplateDeduction::Arguments() const
	{
		TemplateArgs all = m_Args;
		for (const TemplateArg& a : m_Pack) all.Push(a);
		return all;
	}

	QueryResult<void> TemplateDeduction::FromPackArgument(const QualTypeNode& parameter, const Type* argument, uint32 element)
	{
		const uint32 outer = m_Element;
		m_Element = element;
		auto r = FromArgument(parameter, argument);
		m_Element = outer;
		return r;
	}

	QueryResult<void> TemplateDeduction::FromPackTemplateArgument(const AstNode* pattern, const TemplateArg& argument, uint32 element)
	{
		const uint32 outer = m_Element;
		m_Element = element;
		auto r = MatchTemplateArgument(pattern, argument);
		m_Element = outer;
		return r;
	}

	QueryResult<void> TemplateDeduction::FromArgument(const QualTypeNode& parameter, const Type* argument)
	{
		if (!argument || argument->GetKind() == TypeKind::BracedList) return Nothing(); // a braced list: no type to deduce from
		return MatchLayers(parameter, parameter.IndirectionCount(), TypeHandle::UnwrapToValue(argument), true);
	}

	QueryResult<void> TemplateDeduction::FromReceiver(const NamespaceQualifiedNameNode& extended, const Type* receiver)
	{
		return MatchNamed(extended, TypeHandle::UnwrapToValue(receiver), false);
	}

	QueryResult<void> TemplateDeduction::MatchLayers(const QualTypeNode& pattern, uint32 layers, const Type* argument, bool decay)
	{
		// The outermost layer is the last one written: `T*[4]` is an array of pointers.
		if (layers == 0) return MatchBase(pattern, argument, decay && pattern.IndirectionCount() == 0);
		const IndirectionLayer& layer = pattern.GetIndirections()[layers - 1];
		if (layer.arraySizeExpr)
		{
			if (!argument || argument->GetKind() != TypeKind::Array) return Nothing();
			const auto* arr = static_cast<const ArrayType*>(argument);
			if (auto r = MatchValue(layer.arraySizeExpr.Get(), ConstantValue::Int(static_cast<int64>(arr->GetElementCount()))); !r.Ok()) return r;
			return MatchLayers(pattern, layers - 1, arr->GetElementType(), false);
		}
		if (layer.indirectionToken == TokenType::Ampersand) return MatchLayers(pattern, layers - 1, argument, false);
		if (layer.indirectionToken != TokenType::Star || !argument) return Nothing();
		if (argument->GetKind() == TypeKind::Pointer)
			return MatchLayers(pattern, layers - 1, static_cast<const PointerType*>(argument)->GetPointeeType(), false);
		if (argument->GetKind() == TypeKind::Array) // an array argument to a `T*` parameter: its element
			return MatchLayers(pattern, layers - 1, static_cast<const ArrayType*>(argument)->GetElementType(), false);
		return Nothing();
	}

	QueryResult<void> TemplateDeduction::MatchBase(const QualTypeNode& pattern, const Type* argument, bool decay)
	{
		if (const auto* fp = dynamic_cast<const FunPtrTypeNode*>(pattern.FuncPtrType().Get())) return MatchFunPtr(*fp, argument);
		if (!pattern.BaseName()) return Nothing();
		return MatchNamed(*pattern.BaseName(), argument, decay);
	}

	QueryResult<void> TemplateDeduction::MatchNamed(const NamespaceQualifiedNameNode& name, const Type* argument, bool decay)
	{
		if (!argument || name.SegmentCount() == 0) return Nothing();
		const auto& last = name.GetSegments().Back();
		if (last.templateArgs && last.templateArgs->ArgumentCount() > 0)
		{
			const ObjectType* obj = AsObject(argument);
			return obj ? MatchInstance(name, obj) : Nothing();
		}
		if (name.SegmentCount() != 1) return Nothing();
		const int index = ParameterIndex(last.nameToken.GetText());
		if (index < 0 || !TakesType(*m_Params.GetParameter(static_cast<uint32>(index)))) return Nothing();
		TemplateArg arg;
		arg.type = argument;
		if (decay && argument->GetKind() == TypeKind::Array) // a by-value `T` takes an array as a pointer to its element
			arg.type = m_Model.TypeStorePtr()->Pointer(static_cast<const ArrayType*>(argument)->GetElementType());
		return Decide(static_cast<uint32>(index), arg);
	}

	QueryResult<void> TemplateDeduction::MatchInstance(const NamespaceQualifiedNameNode& name, const ObjectType* object)
	{
		// The instance itself, or the base sub-object that is an instance of the named template; a trailing
		// expansion (`Tuple<Ts...>`) takes the instance's remaining arguments, one element each.
		const auto& last = name.GetSegments().Back();
		const TemplatePatterns patterns = TemplatePatterns::OfArguments(last.templateArgs.Get(), &m_Params);
		const uint32 fixed = patterns.patterns.Count();
		const ObjectType* instance = nullptr;
		ClassHierarchy::ForEachSubobject(object, [&](const ObjectType* t, int64) {
			const uint32 count = t->GetTemplateArguments().Count();
			if (!instance && t->GetName() == last.nameToken.GetText() && (patterns.expansion ? count >= fixed : count == fixed)) instance = t;
		});
		if (!instance) return Nothing();
		const auto& given = instance->GetTemplateArguments();
		for (uint32 i = 0; i < given.Count(); ++i)
		{
			auto r = i < fixed ? MatchTemplateArgument(patterns.patterns[i], ArgOf(given[i]))
				: FromPackTemplateArgument(patterns.expansion, ArgOf(given[i]), i - fixed);
			if (!r.Ok()) return r;
		}
		return Nothing();
	}

	QueryResult<void> TemplateDeduction::MatchTemplateArgument(const AstNode* pattern, const TemplateArg& argument)
	{
		if (!argument.IsType()) return MatchValue(pattern, argument.value);
		const auto* q = dynamic_cast<const QualTypeNode*>(pattern);
		return q ? MatchLayers(*q, q->IndirectionCount(), argument.type, false) : Nothing();
	}

	QueryResult<void> TemplateDeduction::MatchFunPtr(const FunPtrTypeNode& pattern, const Type* argument)
	{
		if (!argument || argument->GetKind() != TypeKind::FunPtr) return Nothing();
		const auto* fp = static_cast<const FunPtrType*>(argument);
		if (pattern.GetReturnType())
			if (auto r = MatchLayers(*pattern.GetReturnType(), pattern.GetReturnType()->IndirectionCount(), fp->GetReturnType(), false); !r.Ok()) return r;
		const uint32 count = pattern.ParameterCount() < fp->GetParameterCount() ? pattern.ParameterCount() : fp->GetParameterCount();
		for (uint32 i = 0; i < count; ++i)
		{
			const QualTypeNode* p = pattern.GetParameters()[i]->GetType().Get();
			if (p)
				if (auto r = MatchLayers(*p, p->IndirectionCount(), fp->GetParameterTypes()[i], false); !r.Ok()) return r;
		}
		return Nothing();
	}

	QueryResult<void> TemplateDeduction::MatchValue(const AstNode* pattern, const ConstantValue& value)
	{
		const int index = ParameterIndex(PlainName(pattern));
		if (index < 0 || TakesType(*m_Params.GetParameter(static_cast<uint32>(index)))) return Nothing(); // a written constant
		TemplateArg arg;
		arg.value = value;
		return Decide(static_cast<uint32>(index), arg);
	}

	QueryResult<void> TemplateDeduction::Decide(uint32 index, const TemplateArg& argument)
	{
		if (static_cast<int>(index) == m_PackIndex)
		{
			if (m_PackWritten) return Nothing();
			while (m_Pack.Count() <= m_Element) m_Pack.Push(TemplateArg());
		}
		else if (m_Written[index]) return Nothing(); // a written argument is final; overload resolution checks the conversion
		TemplateArg& slot = static_cast<int>(index) == m_PackIndex ? m_Pack[m_Element] : m_Args[index];
		if (!slot.IsSet()) { slot = argument; return Nothing(); }
		if (slot == argument) return Nothing();
		return Result::Fail(QueryError::Ambiguous, "conflicting deductions for template parameter '" +
			m_Params.GetParameter(index)->GetName() + "': '" + slot.ToString() + "' and '" + argument.ToString() + "'");
	}

	int TemplateDeduction::ParameterIndex(const Core::String& name) const
	{
		if (name.empty()) return -1;
		for (uint32 i = 0; i < m_Params.ParameterCount(); ++i)
			if (m_Params.GetParameter(i)->GetName() == name) return static_cast<int>(i);
		return -1;
	}
} // namespace Scripting
