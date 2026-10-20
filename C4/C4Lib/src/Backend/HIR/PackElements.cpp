#include "Backend/HIR/PackElements.h"

namespace Scripting
{
	QueryResult<PackElements> PackElements::Of(LoweringContext& ctx, const AstNode& pattern)
	{
		auto packs = Packs::In(ctx.model(), &pattern, nullptr);
		if (!packs.Ok()) return ctx.Fail(&pattern, QueryResult<PackElements>::Fail(packs.error, packs.message));
		return PackElements(ctx, &pattern, std::move(packs.value));
	}

	void PackElements::Bind(uint32 index) const
	{
		for (const NamedPack& pack : m_Packs) m_Ctx->queries().RegisterLocal(pack.name, (*pack.elements)[index]);
	}
} // namespace Scripting
