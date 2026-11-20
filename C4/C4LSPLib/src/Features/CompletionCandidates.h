#pragma once
#include "Core/Core.h"
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace C4LSP::Features
{
	struct CompletionItem;

	/// The names a completion offers: each once - the first added wins, so the innermost scope's are added first - and only
	/// those the word being typed starts (any case). A name's detail is made only when the name is kept: a large program's
	/// globals are tens of thousands, of which the typed word keeps a few.
	class CompletionCandidates
	{
	public:
		explicit CompletionCandidates(std::string prefix) : m_Prefix(std::move(prefix)) {}

		/// Whether `name` would be kept: the prefix starts it and it is not offered yet.
		bool Wants(std::string_view name) const;

		/// Offers `name`, its detail made by `detail()`, when it is wanted.
		template <typename MakeDetail>
		void Add(std::string_view name, int kind, MakeDetail&& detail)
		{
			if (Wants(name)) Keep(std::string(name), kind, detail());
		}

		void Add(std::string_view name, int kind, std::string detail)
		{
			if (Wants(name)) Keep(std::string(name), kind, std::move(detail));
		}

		Core::Vector<CompletionItem> Items() const;

		/// Whether `name` starts with `prefix`, any case.
		static bool StartsWith(std::string_view name, std::string_view prefix);

	private:
		void Keep(std::string name, int kind, std::string detail);

		struct Candidate { std::string name; int kind; std::string detail; };
		struct NameHash
		{
			using is_transparent = void;
			size_t operator()(std::string_view name) const { return std::hash<std::string_view>{}(name); }
		};
		std::string m_Prefix;
		std::unordered_set<std::string, NameHash, std::equal_to<>> m_Offered;
		Core::Vector<Candidate> m_Candidates;
	};
} // namespace C4LSP::Features
