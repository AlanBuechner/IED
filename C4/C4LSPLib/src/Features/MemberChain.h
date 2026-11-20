#pragma once
#include "Core/Core.h"
#include <optional>
#include <string>
#include <vector>

namespace Scripting { struct AstNode; class Type; }

namespace C4LSP::Server { class SemanticResolver; }

namespace C4LSP::Features
{
	/// The expression a member access being typed is made on, read from the text before the caret: `GetTransform()->` of
	/// `GetTransform()->Tr|`, `m_Body.Shape.` of `m_Body.Shape.Ra|`, `items[i].` of `items[i].Na|`. Completion offers the
	/// members of its type. It is read from the text, not the tree, so it holds while the analysis is of older text and
	/// while the access does not parse yet.
	class MemberChain
	{
	public:
		/// How a link reaches the next one: `a.b`, `a->b`, `A::b`.
		enum class Access { Dot, Arrow, Scope };
		/// What follows a link's name: a call `f(...)`, an index `a[...]`.
		enum class Suffix { Call, Index };
		/// One name of the chain, what follows it in order, and how the next link (or the member being typed) is reached.
		struct Link
		{
			std::string name;
			std::vector<Suffix> suffixes;
			Access next = Access::Dot;
		};

		/// The chain ending at the `.` or `->` before the word being typed at byte `offset` of `text`; nullopt when the caret
		/// is not after one, or when what it is reached from is not names, calls and indexes (`(x + y).`, a literal).
		static std::optional<MemberChain> Before(const std::string& text, size_t offset);

		/// The type of the chain's value as the compiler resolves its names from `path` (the caret's lexical path) in the file
		/// `unitId`; null when a link does not resolve.
		const Scripting::Type* TypeIn(const Server::SemanticResolver& resolver, uint32 unitId, const Core::Vector<const Scripting::AstNode*>& path) const;

		const std::vector<Link>& Links() const { return m_Links; }

	private:
		explicit MemberChain(std::vector<Link> links) : m_Links(std::move(links)) {}
		std::vector<Link> m_Links; // outermost first
	};
} // namespace C4LSP::Features
