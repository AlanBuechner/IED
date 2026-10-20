#include "Backend/HIR/TemplateBindings.h"

namespace Scripting
{
	void BindTemplateParameters(LoweringContext& ctx, const InstantiationBindings* bindings)
	{
		if (!bindings) return;
		for (const auto& p : bindings->typeParams)
		{
			SymbolInfo sym; sym.kind = SymbolKind::Type; sym.type = p.second; sym.definingUnit = 0;
			ctx.queries().RegisterLocal(p.first, sym);
		}
		for (const auto& p : bindings->valueParams)
		{
			SymbolInfo sym; sym.kind = SymbolKind::Variable; sym.type = nullptr; sym.isConst = true;
			sym.constValue = p.second; sym.definingUnit = 0;
			ctx.queries().RegisterLocal(p.first, sym);
		}
		for (const auto& p : bindings->packParams) ctx.queries().RegisterLocal(p.first, p.second);
	}
} // namespace Scripting
