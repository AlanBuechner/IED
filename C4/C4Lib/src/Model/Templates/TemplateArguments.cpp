#include "Model/Templates/TemplateArguments.h"
#include "Model/ProgramModel.h"
#include "Model/ConstEvaluator.h"
#include "Model/Templates/Packs.h"
#include "Model/Types/QualifiedName.h"
#include "Parser/Nodes/Expressions/PackExpansionNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include <string>

namespace Scripting
{
	namespace
	{
		using ArgResult = QueryResult<TemplateArg>;

		bool SameValue(const ConstantValue& a, const ConstantValue& b)
		{
			if (a.kind == ConstantValue::Kind::Float64 || b.kind == ConstantValue::Kind::Float64) return a.floatValue == b.floatValue && a.kind == b.kind;
			return a.AsInt() == b.AsInt();
		}

		// A value argument in an instance key: equal values (SameValue) give equal text, distinct ones never do.
		Core::String KeyText(const ConstantValue& v)
		{
			if (v.kind == ConstantValue::Kind::Float64) return ConstantText(v);
			return std::to_string(v.AsInt());
		}

		// A bare name (`N`, `Limits::Count`) written where a value is expected: the parser reads every
		// identifier-led argument as a type.
		Core::String NamePath(const QualTypeNode& node)
		{
			if (node.IndirectionCount() || node.HasTemplateArgs() || node.IsLeadingConst() || !node.BaseName()) return "";
			Core::String path;
			for (const auto& seg : node.BaseName()->GetSegments())
			{
				if (!path.empty()) path += "::";
				path += seg.nameToken.GetText();
			}
			return path;
		}

		Core::String ParameterName(const TemplateParameterNode* param, uint32 index)
		{
			return param ? param->GetName() : ("#" + std::to_string(index + 1));
		}
	}

	bool TemplateArg::operator==(const TemplateArg& other) const
	{
		if (IsType() || other.IsType()) return type == other.type;
		return SameValue(value, other.value);
	}

	Core::String TemplateArg::ToString() const
	{
		if (type) return type->GetName();
		return IsSet() ? ConstantText(value) : Core::String("?");
	}

	bool TakesType(const TemplateParameterNode& param)
	{
		return param.Kind() == TemplateParameterKind::Type || param.Kind() == TemplateParameterKind::VariadicType;
	}

	int PackIndex(const TemplateDefinitionNode& params)
	{
		for (uint32 i = 0; i < params.ParameterCount(); ++i)
		{
			const TemplateParameterKind kind = params.GetParameter(i)->Kind();
			if (kind == TemplateParameterKind::VariadicType || kind == TemplateParameterKind::VariadicNonType) return static_cast<int>(i);
		}
		return -1;
	}

	uint32 FixedCount(const TemplateDefinitionNode& params)
	{
		const int pack = PackIndex(params);
		return pack < 0 ? params.ParameterCount() : static_cast<uint32>(pack);
	}

	InstantiationBindings BindingsFor(const ProgramModel& model, const TemplateDefinitionNode& params, const TemplateArgs& args)
	{
		InstantiationBindings bindings;
		const uint32 fixed = FixedCount(params);
		for (uint32 i = 0; i < fixed && i < args.Count(); ++i)
		{
			const Core::String& name = params.GetParameter(i)->GetName();
			if (args[i].IsType()) bindings.typeParams.Push({ name, args[i].type });
			else bindings.valueParams.Push({ name, args[i].value });
		}
		if (PackIndex(params) >= 0)
			bindings.packParams.Push({ params.GetParameter(fixed)->GetName(), TemplateArgumentResolver::PackSymbol(model, *params.GetParameter(fixed), args, fixed) });
		return bindings;
	}

	Core::String ArgumentsKey(const TemplateArgs& args)
	{
		Core::String key = "<";
		for (const TemplateArg& a : args)
		{
			key += a.IsType() ? std::to_string(reinterpret_cast<uintptr_t>(a.type)) : "v" + KeyText(a.value);
			key += ",";
		}
		return key + ">";
	}

	QueryResult<TemplateArgs> TemplateArgumentResolver::Explicit(const ProgramModel& model, const TemplateDefinitionNode& params,
		const TemplateArgumentListNode* written, uint32 unitId, Scope* lookup, const Scope* context, const Core::String& templateName)
	{
		// Each written argument fills the next parameter; once the fixed ones are filled the rest belong to the
		// pack. An expansion (`Ts...`) is the arguments its pattern stands for, in place.
		using Result = QueryResult<TemplateArgs>;
		if (auto pack = CheckPack(params, templateName); !pack.Ok()) return Result::Fail(pack.error, pack.message);
		const int packIndex = PackIndex(params);
		const uint32 fixed = FixedCount(params);
		TemplateArgs args;
		auto parameterAt = [&](uint32 position) -> const TemplateParameterNode* {
			if (position < fixed) return params.GetParameter(position).Get();
			return packIndex >= 0 ? params.GetParameter(static_cast<uint32>(packIndex)).Get() : nullptr;
		};
		auto tooMany = [&]() {
			return Result::Fail(QueryError::NotAValue, "template '" + templateName + "' takes " + std::to_string(params.ParameterCount()) +
				" argument(s), got " + std::to_string(args.Count() + 1));
		};
		for (uint32 i = 0; written && i < written->ArgumentCount(); ++i)
		{
			const AstNode* arg = written->GetArguments()[i].Get();
			if (const auto* expansion = dynamic_cast<const PackExpansionNode*>(arg))
			{
				const TemplateParameterNode* param = parameterAt(args.Count());
				if (!param) return tooMany();
				auto expanded = Packs::ExpandArguments(model, *expansion, TakesType(*param), unitId, lookup, context);
				if (!expanded.Ok()) return Result::Fail(expanded.error, expanded.message);
				for (const TemplateArg& a : expanded.value)
				{
					if (!parameterAt(args.Count())) return tooMany();
					args.Push(a);
				}
				continue;
			}
			const TemplateParameterNode* param = parameterAt(args.Count());
			if (!param) return tooMany();
			auto resolved = Resolve(model, *param, arg, unitId, lookup, context, templateName);
			if (!resolved.Ok()) return Result::Fail(resolved.error, resolved.message);
			args.Push(resolved.value);
		}
		if (args.Count() < fixed) args.Resize(fixed);
		return args;
	}

	QueryResult<TemplateArgs> TemplateArgumentResolver::CompleteAndBind(const ProgramModel& model, const TemplateDefinitionNode& params,
		TemplateArgs given, uint32 unitId, Scope* binding, const Core::String& templateName)
	{
		if (auto pack = CheckPack(params, templateName); !pack.Ok()) return QueryResult<TemplateArgs>::Fail(pack.error, pack.message);
		const uint32 fixed = FixedCount(params);
		if (given.Count() < fixed) given.Resize(fixed);
		for (uint32 i = 0; i < fixed; ++i)
		{
			const TemplateParameterNode& param = *params.GetParameter(i);
			if (!given[i].IsSet() && param.GetDefault())
			{
				auto def = Resolve(model, param, param.GetDefault().Get(), unitId, binding, nullptr, templateName);
				if (!def.Ok()) return QueryResult<TemplateArgs>::Fail(def.error, def.message);
				given[i] = def.value;
			}
			if (!given[i].IsSet())
				return QueryResult<TemplateArgs>::Fail(QueryError::NotFound, "no argument for template parameter '" +
					ParameterName(&param, i) + "' of '" + templateName + "': write it, or give it a default");
			Bind(binding, param, given[i]);
		}
		if (fixed < params.ParameterCount()) BindPack(model, binding, *params.GetParameter(fixed), given, fixed); // possibly empty
		return given;
	}

	bool TemplateArgumentResolver::Complete(const TemplateDefinitionNode& params, const TemplateArgs& given)
	{
		const uint32 fixed = FixedCount(params);
		if (given.Count() < fixed) return false;
		for (uint32 i = 0; i < fixed; ++i)
			if (!given[i].IsSet()) return false;
		return true;
	}

	void TemplateArgumentResolver::BindAll(const ProgramModel& model, Scope* binding, const TemplateDefinitionNode& params, const TemplateArgs& args)
	{
		const uint32 fixed = FixedCount(params);
		for (uint32 i = 0; i < fixed && i < args.Count(); ++i) Bind(binding, *params.GetParameter(i), args[i]);
		if (fixed < params.ParameterCount()) BindPack(model, binding, *params.GetParameter(fixed), args, fixed);
	}

	void TemplateArgumentResolver::BindPack(const ProgramModel& model, Scope* binding, const TemplateParameterNode& pack, const TemplateArgs& args,
		uint32 from)
	{
		binding->Register(pack.GetName(), PackSymbol(model, pack, args, from));
	}

	SymbolInfo TemplateArgumentResolver::PackSymbol(const ProgramModel& model, const TemplateParameterNode& pack, const TemplateArgs& args, uint32 from)
	{
		SymbolInfo sym;
		sym.kind = TakesType(pack) ? SymbolKind::Type : SymbolKind::Variable;
		sym.definingUnit = 0;
		sym.packElements = PackOf(model, args, from);
		return sym;
	}

	SymbolInfo TemplateArgumentResolver::SymbolOf(const TemplateArg& arg)
	{
		SymbolInfo sym;
		sym.definingUnit = 0;
		if (arg.IsType()) { sym.kind = SymbolKind::Type; sym.type = arg.type; }
		else { sym.kind = SymbolKind::Variable; sym.isConst = true; sym.constValue = arg.value; }
		return sym;
	}

	const std::vector<SymbolInfo>* TemplateArgumentResolver::PackOf(const ProgramModel& model, const TemplateArgs& args, uint32 from)
	{
		std::vector<SymbolInfo> elements;
		for (uint32 i = from; i < args.Count(); ++i) elements.push_back(SymbolOf(args[i]));
		return model.StorePack(std::move(elements));
	}

	QueryResult<void> TemplateArgumentResolver::CheckPack(const TemplateDefinitionNode& params, const Core::String& templateName)
	{
		const int pack = PackIndex(params);
		if (pack < 0 || static_cast<uint32>(pack) + 1 == params.ParameterCount()) return QueryResult<void>::Success();
		return QueryResult<void>::Fail(QueryError::NotAValue, "the parameter pack '" + params.GetParameter(static_cast<uint32>(pack))->GetName() +
			"' of '" + templateName + "' must be its last template parameter");
	}

	QueryResult<TemplateArg> TemplateArgumentResolver::Resolve(const ProgramModel& model, const TemplateParameterNode& param,
		const AstNode* written, uint32 unitId, Scope* lookup, const Scope* context, const Core::String& templateName)
	{
		if (TakesType(param) && !dynamic_cast<const QualTypeNode*>(written))
			return ArgResult::Fail(QueryError::NotAValue, "template parameter '" + param.GetName() + "' of '" + templateName + "' takes a type");
		if (TakesType(param)) return ResolveWritten(model, true, written, unitId, lookup, context);
		auto v = ResolveValue(model, written, unitId, lookup, context);
		if (!v.Ok())
			return ArgResult::Fail(v.error, "template parameter '" + param.GetName() + "' of '" + templateName + "' takes a constant value: " + v.message);
		return v;
	}

	QueryResult<TemplateArg> TemplateArgumentResolver::ResolveValue(const ProgramModel& model, const AstNode* written, uint32 unitId, Scope* lookup,
		const Scope* context)
	{
		ConstEvaluator eval(model, context ? context : lookup);
		QueryResult<ConstantValue> folded;
		if (const auto* typeNode = dynamic_cast<const QualTypeNode*>(written))
		{
			const Core::String name = NamePath(*typeNode);
			if (name.empty()) return ArgResult::Fail(QueryError::NotAValue, "a type is not a value");
			folded = eval.EvaluateName(name, unitId, lookup);
		}
		else folded = eval.Evaluate(written, unitId, lookup);
		if (!folded.Ok()) return ArgResult::Fail(folded.error, folded.message);
		TemplateArg arg;
		arg.value = folded.value;
		return arg;
	}

	QueryResult<TemplateArg> TemplateArgumentResolver::ResolveWritten(const ProgramModel& model, bool takesType, const AstNode* written,
		uint32 unitId, Scope* lookup, const Scope* context)
	{
		if (!takesType) return ResolveValue(model, written, unitId, lookup, context);
		const auto* typeNode = dynamic_cast<const QualTypeNode*>(written);
		if (!typeNode) return ArgResult::Fail(QueryError::NotAValue, "a value is not a type");
		auto t = model.ResolveTypeInternal(*typeNode, unitId, lookup, context);
		if (!t.Ok()) return ArgResult::Fail(t.error, t.message);
		TemplateArg arg;
		arg.type = t.value;
		return arg;
	}

	void TemplateArgumentResolver::Bind(Scope* binding, const TemplateParameterNode& param, const TemplateArg& arg)
	{
		binding->Register(param.GetName(), SymbolOf(arg));
	}
} // namespace Scripting
