#include "Model/Builder/ModelResolver.h"
#include "Model/Builder/DeclarationQueries.h"
#include "Model/TypeHandle.h"
#include "Model/IntegerRanges.h"
#include "Model/ConstEvaluator.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"

namespace C4Compiler
{
	namespace
	{
		template <typename T>
		QueryResult<void> Failure(const QueryResult<T>& failed)
		{
			return QueryResult<void>::Fail(failed.error, failed.message, failed.loc);
		}

		bool SameType(const Type* a, const Type* b)
		{
			return TypeHandle(a).ResolveAlias().Raw() == TypeHandle(b).ResolveAlias().Raw();
		}
	}

	// In the scope it is declared in, so `S g;` inside `namespace N` names N::S (BUG-186).
	QueryResult<void> ModelResolver::ResolveVariable(ModelBuilder::PendingVar& v)
	{
		auto type = m_Ctx.model->ResolveTypeInContext(*v.var->GetType(), v.unitId, v.info->declaringScope);
		if (!type.Ok()) return Failure(type);
		v.info->type = type.value;
		return QueryResult<void>::Success();
	}

	// An initializer sees the members before it by their bare names (`B = A + 1`), as in C++: they are bound
	// constants of a layer the evaluator looks in first.
	QueryResult<Core::Vector<int64>> ModelResolver::MemberValues(const ModelBuilder::PendingEnum& e) const
	{
		using Result = QueryResult<Core::Vector<int64>>;
		Core::Vector<int64> values;
		Scope earlier(Scope::Kind::Block, "enum-members");
		int64 next = 0;
		for (const auto& member : e.en->GetMembers())
		{
			int64 value = next;
			if (member->HasInitializer())
			{
				auto folded = ConstEvaluator(*m_Ctx.model, e.scope).Evaluate(member->GetInitializer().Get(), e.unitId, &earlier);
				if (!folded.Ok()) return Result::Fail(folded.error, "member '" + member->GetNameToken().GetText() + "': " + folded.message);
				value = folded.value.AsInt();
			}
			values.Push(value);
			next = static_cast<int64>(static_cast<uint64>(value) + 1);
			SymbolInfo bound;
			bound.kind = SymbolKind::Variable;
			bound.isConst = true;
			bound.constValue = ConstantValue::Int(value);
			earlier.Register(member->GetNameToken().GetText(), bound);
		}
		return Result::Success(values);
	}

	// `enum E { X = 9999999999 }`: an int32 enum cannot hold X.
	void ModelResolver::ReportMembersOutOfRange(const ModelBuilder::PendingEnum& e, const Core::Vector<int64>& values)
	{
		const Type* underlying = e.raw->GetUnderlyingType();
		for (uint32 m = 0; m < values.Count(); ++m)
		{
			if (IntegerRanges::Fits(values[m], underlying)) continue;
			const auto& member = e.en->GetMembers()[m];
			m_Builder.ErrorAt(m_Ctx, member.Get(), "the enum member '" + e.en->GetNameToken().GetText() + "::" + member->GetNameToken().GetText() + "' is " +
				std::to_string(values[m]) + ", outside the range of its underlying type '" + underlying->GetName() + "'");
		}
	}

	QueryResult<void> ModelResolver::ResolveEnum(ModelBuilder::PendingEnum& e)
	{
		const EnumDefinitionNode& en = *e.en;
		if (en.HasUnderlyingType())
		{
			auto underlying = m_Ctx.model->ResolveTypeInContext(*en.GetUnderlyingType(), e.unitId, nullptr);
			if (!underlying.Ok()) return QueryResult<void>::Fail(underlying.error, "underlying type: " + underlying.message, underlying.loc);
			e.raw->SetUnderlyingType(underlying.value);
		}
		auto values = MemberValues(e);
		if (!values.Ok()) return Failure(values);
		ReportMembersOutOfRange(e, values.value);
		for (uint32 m = 0; m < en.GetMembers().Count(); ++m)
		{
			EnumType::MemberInfo member;
			member.mName = en.GetMembers()[m]->GetNameToken().GetText();
			member.m_Value = values.value[m];
			member.m_DeclaringNode = en.GetMembers()[m].Get();
			e.raw->AddMember(member);
		}
		return QueryResult<void>::Success();
	}

	// The target resolves in the alias's own scope: `alias IWedge = Wedge<int32>` inside `namespace Weld` binds Weld's
	// Wedge, not a same-named global template. An alias declared again names the type the first one does (BUG-273).
	QueryResult<void> ModelResolver::ResolveAlias(ModelBuilder::PendingAlias& a)
	{
		auto target = m_Ctx.model->ResolveTypeInContext(*a.target, a.unitId, a.scope);
		if (!target.Ok()) return Failure(target);
		if (a.repeats)
			if (auto first = m_Queries.Alias(*a.repeats); !first.Ok()) return first;
		a.raw->SetAliasedType(target.value);
		if (a.repeats && !SameType(target.value, a.repeats->GetAliasedType()))
			m_Builder.ErrorAt(m_Ctx, a.declaration, "redefinition of alias '" + a.raw->GetName() + "' as a different type: '" +
				target.value->ToString() + "', was '" + a.repeats->GetAliasedType()->ToString() + "'");
		return QueryResult<void>::Success();
	}

	// In the scope the union is declared in, so `S s;` inside `namespace N` names N::S (BUG-191).
	QueryResult<void> ModelResolver::ResolveUnion(ModelBuilder::PendingUnion& u)
	{
		QueryResult<void> failed = QueryResult<void>::Success();
		Core::Vector<ObjectType::FieldInfo> fields;
		ForEachUnionField(*u.un, [&](const VariableDefinitionNode& var, int view) {
			if (!failed.Ok()) return;
			auto type = m_Ctx.model->ResolveTypeInContext(*var.GetType(), u.unitId, u.scope);
			if (!type.Ok())
			{
				failed = Failure(type);
				u.failedField = &var;
				return;
			}
			ObjectType::FieldInfo f;
			f.mName = var.GetNameToken().GetText();
			f.m_Type = type.value;
			f.m_IsConst = var.IsConst();
			f.m_IsVolatile = var.IsVolatile();
			f.m_Access = var.GetAccessModifier();
			f.m_DeclaringNode = &var;
			f.m_UnionViewId = view;
			fields.Push(f);
		});
		if (!failed.Ok()) return failed;
		for (const auto& f : fields) u.raw->AddField(f);
		u.raw->CalculateLayout(); // named unions need a concrete size for sizeof / slot allocas
		u.raw->SetPendingFields(false);
		return QueryResult<void>::Success();
	}
} // namespace C4Compiler
