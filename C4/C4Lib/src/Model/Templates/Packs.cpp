#include "Model/Templates/Packs.h"
#include "Model/ProgramModel.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/PackExpansionNode.h"
#include <string>

namespace Scripting
{
	namespace
	{
		// The single-segment names in `node` (identifiers, type names), not those inside a nested expansion.
		void CollectNames(const AstNode* node, std::vector<Core::String>& names)
		{
			if (!node || dynamic_cast<const PackExpansionNode*>(node)) return;
			if (const auto* id = dynamic_cast<const IdentifierExpressionNode*>(node)) names.push_back(id->GetName());
			if (const auto* qualified = dynamic_cast<const NamespaceQualifiedNameNode*>(node))
			{
				if (qualified->SegmentCount() == 1) names.push_back(qualified->GetSegments()[0].nameToken.GetText());
				for (const auto& segment : qualified->GetSegments()) CollectNames(segment.templateArgs.Get(), names); // `Box<Ts>`
			}
			if (const auto* call = dynamic_cast<const FunctionCallNode*>(node)) CollectNames(call->GetTemplateArgs().Get(), names); // `Make<Ts>()`
			for (const auto& child : node->Children()) CollectNames(child.Get(), names);
		}
	}

	QueryResult<std::vector<NamedPack>> Packs::In(const ProgramModel& model, const AstNode* pattern, const Scope* lookup)
	{
		using Result = QueryResult<std::vector<NamedPack>>;
		std::vector<Core::String> names;
		CollectNames(pattern, names);
		std::vector<NamedPack> packs;
		for (const Core::String& name : names)
		{
			const SymbolInfo* sym = Find(model, name, lookup);
			if (!sym) continue;
			bool seen = false;
			for (const NamedPack& p : packs) seen = seen || p.name == name;
			if (seen) continue;
			if (!packs.empty() && packs[0].elements->size() != sym->packElements->size())
				return Result::Fail(QueryError::NotAValue, "the packs '" + packs[0].name + "' (" + std::to_string(packs[0].elements->size()) +
					") and '" + name + "' (" + std::to_string(sym->packElements->size()) + ") in one expansion have different lengths");
			packs.push_back({ name, sym->packElements });
		}
		if (packs.empty())
			return Result::Fail(QueryError::NotAValue, "'" + (pattern ? pattern->ToString() : Core::String()) + "...' names no pack to expand");
		return Result::Success(packs);
	}

	bool Packs::Names(const ProgramModel& model, const AstNode* pattern, const Scope* lookup)
	{
		std::vector<Core::String> names;
		CollectNames(pattern, names);
		for (const Core::String& name : names)
			if (Find(model, name, lookup)) return true;
		return false;
	}

	const SymbolInfo* Packs::Find(const ProgramModel& model, const Core::String& name, const Scope* lookup)
	{
		const SymbolInfo* sym = lookup ? lookup->Find(name) : nullptr;
		if (!sym) sym = model.FindLocal(name);
		return sym && sym->IsPack() ? sym : nullptr;
	}

	uint32 Packs::Length(const std::vector<NamedPack>& packs)
	{
		return packs.empty() ? 0 : static_cast<uint32>(packs[0].elements->size());
	}

	Scope* Packs::ElementScope(const ProgramModel& model, const std::vector<NamedPack>& packs, uint32 index, Scope* parent)
	{
		Scope* scope = model.Symbols()->CreateScope(Scope::Kind::Block, "pack-element", parent ? parent : const_cast<Scope*>(model.Symbols()->GetRoot()));
		for (const NamedPack& p : packs) scope->Register(p.name, (*p.elements)[index]);
		return scope;
	}

	QueryResult<TemplateArgs> Packs::ExpandArguments(const ProgramModel& model, const PackExpansionNode& expansion, bool takesType,
		uint32 unitId, Scope* lookup, const Scope* context)
	{
		using Result = QueryResult<TemplateArgs>;
		auto packs = In(model, expansion.GetPattern().Get(), lookup);
		if (!packs.Ok()) return Result::Fail(packs.error, packs.message);
		TemplateArgs args;
		for (uint32 i = 0; i < Length(packs.value); ++i)
		{
			auto arg = TemplateArgumentResolver::ResolveWritten(model, takesType, expansion.GetPattern().Get(), unitId,
				ElementScope(model, packs.value, i, lookup), context);
			if (!arg.Ok()) return Result::Fail(arg.error, arg.message);
			args.Push(arg.value);
		}
		return Result::Success(args);
	}
} // namespace Scripting
