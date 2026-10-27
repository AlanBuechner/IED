#pragma once
#include "Model/Types/ObjectType.h" // FieldInfo (unions reuse the object field descriptor)

namespace Scripting
{
	class Scope;

	/// A union type: members share one storage region; size is the largest member, alignment the strictest.
	/// A member is a field at offset 0, or a field of a struct view (`struct { float x; float y; };`), laid out
	/// one after another from offset 0.
	class UnionType : public Type
	{
	public:
		UnionType(const Core::String& name);
		Core::String GetName() const override;
		Core::String ToString() const override { return GetName(); }

		const Core::String& GetFullName() const { return m_FullName; }
		void SetFullName(const Core::String& name) { m_FullName = name; }

		const Core::Vector<ObjectType::FieldInfo>& GetFields() const { return m_Fields; }
		void AddField(const ObjectType::FieldInfo& field) { m_Fields.Push(field); }
		// The resolver adds a named union's fields together once every one resolves (BUG-393).
		bool HasPendingFields() const { return m_PendingFields; }
		void SetPendingFields(bool pending) { m_PendingFields = pending; }
		void CalculateLayout();

		int64 GetFieldOffset(const Core::String& name) const;

		const AstNode* GetDeclNode() const { return m_DeclNode; }
		void SetDeclNode(const AstNode* node) { m_DeclNode = node; }
		/// The scope the union is declared in: where its member types resolve and its annotations fold.
		const Scope* GetDeclScope() const { return m_DeclScope; }
		void SetDeclScope(const Scope* scope) { m_DeclScope = scope; }
		/// The namespaces and classes the union is declared in, outermost first.
		const Core::Vector<Core::String>& GetEnclosingNames() const { return m_EnclosingNames; }
		void SetEnclosingNames(const Core::Vector<Core::String>& names) { m_EnclosingNames = names; }
		/// `[group("name")]` annotations on the declaration.
		const Core::Vector<Core::String>& GetGroups() const { return m_Groups; }
		void SetGroups(const Core::Vector<Core::String>& groups) { m_Groups = groups; }

	private:
		Core::String m_FullName;
		Core::Vector<ObjectType::FieldInfo> m_Fields;
		bool m_PendingFields = false;
		const AstNode* m_DeclNode = nullptr;
		const Scope* m_DeclScope = nullptr;
		Core::Vector<Core::String> m_EnclosingNames;
		Core::Vector<Core::String> m_Groups;
	};
} // namespace Scripting
