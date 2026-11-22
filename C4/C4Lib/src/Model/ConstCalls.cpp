// D-91: a constant that calls a [compile_time] function (ConstEvaluator::FoldCall).
#include "Model/ConstEvaluator.h"
#include "Model/CompileTimeCalls.h"
#include "Model/Builder/DeclarationQueries.h"
#include "Model/CompileTimeMark.h"
#include "Model/ProgramModel.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/ParenthesizedExpressionNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include <limits>
#include <vector>

namespace C4Compiler
{
	namespace
	{
		// The name a call is written with (`Square`, `Tables::Square`), or "" when its callee is no name.
		Core::String CalleeName(const AstNode* callee)
		{
			if (const auto* name = dynamic_cast<const IdentifierExpressionNode*>(callee)) return name->GetName();
			const auto* access = dynamic_cast<const StaticAccessNode*>(callee);
			if (!access) return Core::String();
			const Core::String base = CalleeName(access->GetBaseType().Get());
			return base.empty() ? Core::String() : base + "::" + access->GetMemberNameToken().GetText();
		}

		// Whether a call of `function` may run in a constant at all: one of its overloads is marked, or it is a template
		// (an instance's mark is its template's). Asked before overloads resolve, so an unmarked call instantiates nothing.
		bool MayBeCompileTime(const SymbolInfo& function)
		{
			if (function.templateDef) return true;
			for (const FunctionRecord* record : function.functions)
				if (record && IsCompileTime(*record)) return true;
			return false;
		}
	}

	const Type* ConstEvaluator::ArgumentType(const AstNode* argument, const ConstantValue& value, uint32 unitId) const
	{
		if (auto written = ExpressionType(argument, unitId); written.Ok() && written.value) return written.value;
		TypeStore* types = m_Model.TypeStorePtr();
		switch (value.kind)
		{
		case ConstantValue::Kind::Bool: return types->Builtin(TypeKind::Bool);
		case ConstantValue::Kind::Float64: return types->Builtin(TypeKind::Float64);
		case ConstantValue::Kind::Int64:
			return value.intValue >= std::numeric_limits<int32>::min() && value.intValue <= std::numeric_limits<int32>::max()
				? types->Builtin(TypeKind::Int32) : types->Builtin(TypeKind::Int64);
		default: return nullptr;
		}
	}

	QueryResult<ConstantValue> ConstEvaluator::FoldCall(const FunctionCallNode& call, uint32 unitId, Visiting& visiting, Scope* bindingScope) const
	{
		auto resolved = ResolveCall(call, unitId, visiting, bindingScope);
		if (!resolved.Ok()) return QueryResult<ConstantValue>::Fail(resolved.error, resolved.message, resolved.loc);
		return resolved.value.calls->Call(*resolved.value.record, resolved.value.arguments, call.startLocation);
	}

	QueryResult<std::vector<uint8>> ConstEvaluator::EvaluateData(const AstNode* expr, uint32 unitId) const
	{
		using Result = QueryResult<std::vector<uint8>>;
		while (const auto* paren = dynamic_cast<const ParenthesizedExpressionNode*>(expr)) expr = paren->GetExpression().Get();
		const auto* call = dynamic_cast<const FunctionCallNode*>(expr);
		if (!call) return Result::Fail(QueryError::NotAValue, "not a call of a [compile_time] function");
		Visiting visiting;
		auto resolved = ResolveCall(*call, unitId, visiting, nullptr);
		if (!resolved.Ok()) return Result::Fail(resolved.error, resolved.message, resolved.loc);
		return resolved.value.calls->CallForData(*resolved.value.record, resolved.value.arguments, call->startLocation);
	}

	QueryResult<ConstEvaluator::CompileTimeCall> ConstEvaluator::ResolveCall(const FunctionCallNode& call, uint32 unitId, Visiting& visiting,
		Scope* bindingScope) const
	{
		using Result = QueryResult<CompileTimeCall>;
		const Core::String name = CalleeName(call.GetCallee().Get());
		// While the model resolves, functions come last: a constant in an enum or an extent asks for the callee's first.
		if (DeclarationQueries* pending = m_Model.Pending(); pending && !name.empty())
			if (auto resolved = pending->Functions(name); !resolved.Ok()) return Result::Fail(resolved.error, resolved.message, resolved.loc);
		auto function = name.empty() ? QueryResult<const SymbolInfo*>() : m_Model.QuerySymbol(name, unitId, m_Context);
		if (name.empty() || !function.Ok() || !function.value || function.value->kind != SymbolKind::Function)
			return Result::Fail(QueryError::NotAValue, "'" + call.ToString() + "' is not one of the forms a constant folds from "
				"(literals, constants, enum members, sizeof and alignof, operators on them, and calls of [compile_time] functions)");
		if (!MayBeCompileTime(*function.value))
			return Result::Fail(QueryError::NotAValue, "'" + name + "' is not [compile_time]: only a function marked [compile_time] runs in a constant");

		std::vector<ConstantValue> values;
		Core::Vector<const Type*> types;
		for (const auto& argument : call.GetArguments())
		{
			auto value = EvaluateInner(argument.Get(), unitId, visiting, bindingScope);
			if (!value.Ok()) return Result::Fail(value.error, value.message, value.loc);
			const Type* type = ArgumentType(argument.Get(), value.value, unitId);
			if (!type) return Result::Fail(QueryError::NotAValue, "'" + argument->ToString() + "' cannot be passed to '" + name + "' at compile time");
			values.push_back(value.value);
			types.Push(type);
		}

		LookupContext context;
		context.unitId = unitId;
		context.scope = m_Context;
		const FunctionRecord* record = nullptr;
		auto signature = m_Model.QueryFunction(name, nullptr, call.GetTemplateArgs().Get(), types, context, &record);
		if (!signature.Ok()) return Result::Fail(signature.error, signature.message, signature.loc);
		if (!record) return Result::Fail(QueryError::NotFound, "'" + name + "' has no definition to run at compile time");
		ICompileTimeCalls* calls = m_Model.CompileTimeCalls();
		if (!calls) return Result::Fail(QueryError::Unsupported, "compile-time evaluation is not available here: '" + name + "' cannot run");
		return Result::Success(CompileTimeCall{ record, std::move(values), calls });
	}
} // namespace C4Compiler
