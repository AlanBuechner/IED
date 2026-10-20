#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Represents a C/C++ interop declaration in C4 source.
	///
	/// Examples:
	///   extern "c" include "windows.h";
	///   extern "c++" include "mylib.hpp";
	///   extern "c" source "native.c";
	///   extern "c" include "d3d12.h" { define("D3D12_SDK_VERSION", "800"); include_dir("C:/SDK/Include"); }
	///
	/// The language string selects the C dialect ("c" or "c++"). `include` runs CBind (a
	/// ClangTool subprocess) over the header and generates C4 declarations; `source`
	/// compiles the file with the bundled clang and links its object into the output.
	struct ExternDeclarationNode : public AstNode
	{
		enum class Kind { Include, Source };

		struct Define
		{
			Core::String name;
			Core::String value; // empty = defined as 1
		};

		ExternDeclarationNode() = default;

		// --- Getters ---
		const Core::String& Language() const { return m_Language; }
		Kind GetKind() const { return m_Kind; }
		bool IsInclude() const { return m_Kind == Kind::Include; }
		bool IsSource() const { return m_Kind == Kind::Source; }
		const Core::String& FilePath() const { return m_PathToken.GetText(); }
		const Token& GetFilePathToken() const { return m_PathToken; }
		const Core::Vector<Define>& Defines() const { return m_Defines; }
		const Core::Vector<Core::String>& IncludeDirs() const { return m_IncludeDirs; }
		const Core::Vector<Core::String>& Libraries() const { return m_Libraries; }

		/// Factory: peeks for the `extern` keyword, creates a Ref<ExternDeclarationNode>,
		/// and calls member ParseBody. Returns an empty Ref on failure without consuming
		/// any tokens (keyword match is irrevocable once seen).
		static Ref<ExternDeclarationNode> TryParse(ParserState& state);

		/// Member: called after the `extern` keyword has been consumed. Parses the language
		/// string, the include/source kind, the path string, and an optional config block.
		void ParseBody(ParserState& state);

		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::ExternDeclaration; }

	private:
		Core::String m_Language; // "c" or "c++"
		Kind m_Kind = Kind::Include;
		Token m_PathToken; // path string token (content without quotes)
		Core::Vector<Define> m_Defines;
		Core::Vector<Core::String> m_IncludeDirs;
		Core::Vector<Core::String> m_Libraries;
	};
}
