#pragma once
#include "Backend/HIR/LoweringContext.h"
#include "Model/Templates/Packs.h"

namespace Scripting
{
	/// The copies a pack expansion stands for in a body (docs/language/08 6): copy i is the pattern lowered with
	/// each pack it names meaning that pack's element i.
	class PackElements
	{
	public:
		PackElements() = default;
		/// The copies of `pattern`: an error when it names no pack, or packs of different lengths.
		static QueryResult<PackElements> Of(LoweringContext& ctx, const AstNode& pattern);

		uint32 Count() const { return Packs::Length(m_Packs); }
		const AstNode* Pattern() const { return m_Pattern; }

		/// `lower()` run where each pack means its element `index`.
		template <typename Lower> auto Within(uint32 index, Lower&& lower) const -> decltype(lower())
		{
			ScopeGuard element = m_Ctx->queries().PushScope(Scope::Kind::Block, "pack-element");
			Bind(index);
			return lower();
		}

		/// `lower(index)` for each copy in order, stopping at the first failure.
		template <typename Lower> QueryResult<void> ForEach(Lower&& lower) const
		{
			for (uint32 i = 0; i < Count(); ++i)
				if (auto r = Within(i, [&] { return lower(i); }); !r.Ok()) return r;
			return QueryResult<void>::Success();
		}

	private:
		PackElements(LoweringContext& ctx, const AstNode* pattern, std::vector<NamedPack> packs)
			: m_Ctx(&ctx), m_Pattern(pattern), m_Packs(std::move(packs)) {}
		void Bind(uint32 index) const;

		LoweringContext* m_Ctx = nullptr;
		const AstNode* m_Pattern = nullptr;
		std::vector<NamedPack> m_Packs;
	};
} // namespace Scripting
