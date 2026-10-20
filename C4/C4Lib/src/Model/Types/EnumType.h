#pragma once
#include "Model/Types/Type.h"
#include "Parser/AstNode.h" // AstNode (declaration node for LSP/DAP)

namespace Scripting
{
	class Scope;

	/// An enumeration type. Members are (name, value) pairs; the underlying type fixes size/alignment.
	class EnumType : public Type
	{
	public:
		struct MemberInfo
		{
			Core::String mName;
			int64 m_Value = 0;
		};

		EnumType(const Core::String& name, const Type* underlyingType);
		Core::String GetName() const override;
		Core::String ToString() const override { return GetName(); }

		const Core::String& GetFullName() const { return m_FullName; }
		void SetFullName(const Core::String& name) { m_FullName = name; }

		const Type* GetUnderlyingType() const { return m_UnderlyingType; }
		// The COLLECT pre-pass registers a shell with a default (int32) underlying before the real
		// one is resolved; ProcessEnumDefinition calls this to correct it so size/alignment match.
		void SetUnderlyingType(const Type* t)
		{
			if (t == m_UnderlyingType) return;
			m_UnderlyingType = t;
			if (t) { m_Size = t->GetSize(); m_Alignment = t->GetAlignment(); }
		}
		const Core::Vector<MemberInfo>& GetMembers() const { return m_Members; }
		void AddMember(const MemberInfo& member) { m_Members.Push(member); }
		uint32 GetMemberCount() const { return m_Members.Count(); }
		const MemberInfo* GetMemberByName(const Core::String& name) const;

		// 'enum class' / 'enum struct': members are not injected into the enclosing scope and are
		// referenced only as Color::Red. Plain enums inject members unscoped (C-style).
		bool IsScoped() const { return m_IsScoped; }
		void SetScoped(bool scoped) { m_IsScoped = scoped; }

		const AstNode* GetDeclNode() const { return m_DeclNode; }
		void SetDeclNode(const AstNode* node) { m_DeclNode = node; }

		/// The scope the enum is declared in: where its annotations fold.
		const Scope* GetDeclScope() const { return m_DeclScope; }
		void SetDeclScope(const Scope* scope) { m_DeclScope = scope; }

		/// The namespaces and classes the enum is declared in, outermost first.
		const Core::Vector<Core::String>& GetEnclosingNames() const { return m_EnclosingNames; }
		void SetEnclosingNames(const Core::Vector<Core::String>& names) { m_EnclosingNames = names; }

	private:
		Core::String m_FullName;
		const Type* m_UnderlyingType = nullptr;
		Core::Vector<MemberInfo> m_Members;
		const AstNode* m_DeclNode = nullptr;
		const Scope* m_DeclScope = nullptr;
		Core::Vector<Core::String> m_EnclosingNames;
		bool m_IsScoped = false;
	};
} // namespace Scripting
