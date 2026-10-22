#include "Backend/HIR/ModelQueries.h"
#include "Model/TypeNotFound.h"
#include "Backend/HIR/HIRNodes.h" // IntrinsicOp
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Model/TypeHandle.h" // UnwrapToValue
#include "Model/Types/ClassHierarchy.h"

namespace Scripting
{
	// Token -> built-in op for PRIMITIVES. Null = not a primitive op (the caller tried an operator
	// method first; if that also failed it is an error). Mirrors the model's OperatorTable so the
	// two phases cannot disagree about what a token means. Function-local statics keep one instance.
	const IntrinsicOp* ModelQueries::IntrinsicForBinary(const Core::String& tok)
	{
		static const IntrinsicOp kAdd = IntrinsicOp::Add, kSub = IntrinsicOp::Sub, kMul = IntrinsicOp::Mul,
			kDiv = IntrinsicOp::Div, kMod = IntrinsicOp::Mod, kPow = IntrinsicOp::Pow, kEq = IntrinsicOp::Eq, kNe = IntrinsicOp::Ne,
			kLt = IntrinsicOp::Lt, kGt = IntrinsicOp::Gt, kLe = IntrinsicOp::Le, kGe = IntrinsicOp::Ge,
			kBitAnd = IntrinsicOp::BitAnd, kBitOr = IntrinsicOp::BitOr, kBitXor = IntrinsicOp::BitXor,
			kShl = IntrinsicOp::Shl, kShr = IntrinsicOp::Shr;
		if (tok == "+") return &kAdd;  if (tok == "-") return &kSub;  if (tok == "*") return &kMul;
		if (tok == "/") return &kDiv;  if (tok == "%") return &kMod;  if (tok == "**") return &kPow;
		if (tok == "==") return &kEq;  if (tok == "!=") return &kNe;
		if (tok == "<") return &kLt;   if (tok == ">") return &kGt;   if (tok == "<=") return &kLe;
		if (tok == ">=") return &kGe;
		if (tok == "&") return &kBitAnd; if (tok == "|") return &kBitOr; if (tok == "^") return &kBitXor;
		if (tok == "<<") return &kShl;  if (tok == ">>") return &kShr;
		return nullptr;
	}

	const IntrinsicOp* ModelQueries::IntrinsicForUnary(const Core::String& tok)
	{
		static const IntrinsicOp kNegate = IntrinsicOp::Negate, kBitNot = IntrinsicOp::BitNot, kBoolNot = IntrinsicOp::BoolNot;
		if (tok == "-") return &kNegate; if (tok == "~") return &kBitNot; if (tok == "!") return &kBoolNot;
		return nullptr;
	}

	// The namespaces of a qualified name descend from the root, as in QueryNqnType.
	QueryResult<const Type*> ModelQueries::QueryTemplateInstance(const Core::String& name, const TemplateArgumentListNode& args,
		uint32 unitId, const Scope* scopeContext) const
	{
		const SymbolTable* sym = m_Model->Symbols();
		const size_t last = name.rfind("::");
		if (last == Core::String::npos)
			return m_Model->InstantiateTypeTemplate(name, scopeContext ? scopeContext : sym->GetRoot(), args, unitId, nullptr, scopeContext);
		const Scope* from = sym->GetRoot();
		for (size_t start = 0; start < last;)
		{
			const size_t end = name.find("::", start);
			const Core::String segment = name.substr(start, end - start);
			const SymbolInfo* info = sym->FindVisible(from, segment, unitId);
			if (!info || info->kind != SymbolKind::Namespace || !info->scope)
				return QueryResult<const Type*>::Fail(QueryError::NotFound, "not a namespace: " + segment);
			from = info->scope;
			start = end + 2;
		}
		return m_Model->InstantiateTypeTemplate(name.substr(last + 2), from, args, unitId, nullptr, scopeContext);
	}

	const Type* ModelQueries::ResolveNqnType(const NamespaceQualifiedNameNode* nqn, uint32 unitId, const Scope* scopeContext) const
	{
		return QueryNqnType(nqn, unitId, scopeContext).value;
	}

	// Each segment narrows where the next is found, as in TypeResolver: a namespace, a type's class layer
	// (`Outer::Inner`) or an instance's (`Outer<float>::Inner`, BUG-179). The first segment is found from the
	// asking context outward.
	QueryResult<const Type*> ModelQueries::QueryNqnType(const NamespaceQualifiedNameNode* nqn, uint32 unitId, const Scope* scopeContext) const
	{
		using Result = QueryResult<const Type*>;
		if (!nqn || nqn->SegmentCount() == 0) return Result::Fail(QueryError::NotFound, "empty type name");
		const Scope* from = scopeContext ? scopeContext : m_Model->Symbols()->GetRoot();
		for (uint32 i = 0; i < nqn->SegmentCount(); ++i)
		{
			const auto& seg = nqn->GetSegments()[i];
			auto segment = SegmentType(seg, from, unitId, scopeContext);
			if (!segment.Ok()) return segment;
			if (i + 1 == nqn->SegmentCount())
				return segment.value ? Result(TypeHandle::UnwrapToValue(segment.value)) : Result::Fail(QueryError::NotFound, TypeNotFound(seg.nameToken.GetText()));
			if (segment.value)
			{
				const auto* obj = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(segment.value));
				from = obj ? m_Model->TypeScope(obj) : nullptr;
				if (!from) return Result::Fail(QueryError::NotFound, "not a class: " + seg.nameToken.GetText());
				continue;
			}
			const SymbolInfo* info = m_Model->Symbols()->FindVisible(from, seg.nameToken.GetText(), unitId);
			if (!info || info->kind != SymbolKind::Namespace || !info->scope) return Result::Fail(QueryError::NotFound, "not a namespace: " + seg.nameToken.GetText());
			from = info->scope;
		}
		return Result::Fail(QueryError::NotFound, "empty type name");
	}

	// The type one segment names from `from`: an instance for template arguments, a declared type, or null for
	// anything else (a namespace).
	QueryResult<const Type*> ModelQueries::SegmentType(const NamespaceQualifiedNameNode::Segment& seg, const Scope* from, uint32 unitId,
		const Scope* scopeContext) const
	{
		const Core::String& name = seg.nameToken.GetText();
		if (seg.templateArgs) return m_Model->InstantiateTypeTemplate(name, from, *seg.templateArgs, unitId, nullptr, scopeContext);
		const SymbolInfo* info = m_Model->Symbols()->FindVisible(from, name, unitId);
		if (info && info->kind == SymbolKind::Type) return QueryResult<const Type*>(info->type);
		if (!info) if (const Type* builtin = m_Model->TypeStorePtr()->FindBuiltin(name)) return QueryResult<const Type*>(builtin);
		return QueryResult<const Type*>(nullptr);
	}

	const FunctionRecord* ModelQueries::FindOperatorMethod(const ObjectType* t, const Core::String& annotation, uint32 argc) const
	{
		// Own methods first, then every base sub-object; an exact arity match wins, else the first found.
		const FunctionRecord* exact = nullptr;
		const FunctionRecord* fallback = nullptr;
		ClassHierarchy::ForEachSubobject(t, [&](const ObjectType* cur, int64) {
			if (exact) return;
			for (uint32 i = 0; i < cur->GetMethodCount() && !exact; ++i)
			{
				const auto& m = cur->GetMethods()[i];
				if (!m.HasOperator(annotation)) continue;
				const FunctionRecord* rec = RecordForMethod(cur, &cur->GetMethods()[i]);
				if (!rec) continue;
				uint32 pcount = rec->type ? rec->type->GetParameterCount() : 0;
				if (pcount == argc) exact = rec;
				else if (!fallback) fallback = rec;
			}
		});
		return exact ? exact : fallback;
	}

	const FunctionRecord* ModelQueries::FindCastMethod(const Type* source, const Type* target) const
	{
		if (!source || !target) return nullptr;
		source = TypeHandle::UnwrapToValue(source);
		target = TypeHandle::UnwrapToValue(target);
		// Object receiver: a [operator("cast")] member (or inherited) on the source type. The cast takes no
		// explicit argument (this IS the operand), so pick the zero-arity overload and confirm it returns the
		// target type - a type may declare several cast overloads to different targets.
		if (const auto* obj = dynamic_cast<const ObjectType*>(source))
		{
			const FunctionRecord* found = nullptr;
			ClassHierarchy::ForEachSubobject(obj, [&](const ObjectType* cur, int64) {
				if (found) return;
				for (uint32 i = 0; i < cur->GetMethodCount(); ++i)
				{
					if (!cur->GetMethods()[i].HasOperator("cast")) continue;
					const FunctionRecord* rec = RecordForMethod(cur, &cur->GetMethods()[i]);
					if (rec && rec->type && rec->type->GetParameterCount() == 0 && TypeHandle::UnwrapToValue(rec->type->GetReturnType()) == target)
					{ found = rec; return; }
				}
			});
			if (found) return found;
		}
		// Primitive receiver: an `extends float` cast is in no method list, only in the primitive extensions.
		const Core::Vector<const FunctionRecord*>* extensions = m_Model->PrimitiveExtensions(source);
		if (!extensions) return nullptr;
		for (const FunctionRecord* rec : *extensions)
		{
			if (!rec->type || TypeHandle::UnwrapToValue(rec->type->GetReturnType()) != target) continue;
			for (const auto& ann : rec->operatorAnnotations)
				if (ann == "cast") return rec;
		}
		return nullptr;
	}
} // namespace Scripting
