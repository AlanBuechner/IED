#include "Backend/Lowering/GlobalStorage.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/FunPtrType.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"

namespace Scripting
{
	namespace
	{
		// The name an initializer gives: a bare or qualified identifier; empty for any other expression.
		Core::String NamedFunction(const AstNode* initializer)
		{
			if (const auto* id = dynamic_cast<const IdentifierExpressionNode*>(initializer)) return id->GetName();
			Core::String name;
			if (const auto* qualified = dynamic_cast<const NamespaceQualifiedNameNode*>(initializer))
				for (uint32 i = 0; i < qualified->SegmentCount(); ++i)
				{
					if (i) name += "::";
					name += qualified->GetSegments()[i].nameToken.GetText();
				}
			return name;
		}

		// A folded string or number is the global's start: storage that starts holding it.
		bool StartsAsFolded(const SymbolInfo& global)
		{
			return global.constValue.kind == ConstantValue::Kind::String || (!global.constValue.IsNone() && global.constValue.kind != ConstantValue::Kind::Guid);
		}
	}

	std::vector<const SymbolInfo*> GlobalsWithStorage(const ProgramModel& model)
	{
		std::vector<const SymbolInfo*> globals;
		model.Symbols()->ForEachGlobalVariable([&](const SymbolInfo& info, const Scope&) {
			if (info.kind == SymbolKind::Variable && info.type && !info.mangledName.empty() && info.linkSymbol.empty()) globals.push_back(&info);
		});
		return globals;
	}

	const FunctionRecord* FunctionPointerStart(ProgramModel& model, const SymbolInfo& global)
	{
		const Type* value = global.type ? TypeHandle::UnwrapToValue(global.type) : nullptr;
		if (!global.initializer || !value || value->GetKind() != TypeKind::FunPtr || StartsAsFolded(global)) return nullptr;
		const Core::String name = NamedFunction(global.initializer);
		if (name.empty()) return nullptr;
		Core::Vector<const Type*> parameters;
		for (const Type* parameter : static_cast<const FunPtrType*>(value)->GetParameterTypes()) parameters.Push(parameter);
		const FunctionRecord* record = nullptr;
		const auto found = model.QueryFunction(name, nullptr, nullptr, parameters, LookupContext{ global.definingUnit, nullptr, nullptr }, &record);
		return found.Ok() ? record : nullptr;
	}
} // namespace Scripting
