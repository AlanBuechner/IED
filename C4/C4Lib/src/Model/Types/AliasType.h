#pragma once
#include "Model/Types/Type.h"

namespace Scripting
{
	/// A type alias (C++ `using`): a pure synonym for another type, transparent for value identity.
	class AliasType : public Type
	{
	public:
		AliasType(const Core::String& name, const Type* aliasedType);
		Core::String GetName() const override;
		Core::String ToString() const override { return GetName(); }

		const Core::String& GetFullName() const { return m_FullName; }
		void SetFullName(const Core::String& name) { m_FullName = name; }
		const Type* GetAliasedType() const { return m_AliasedType; }
		// Pass-2 resolution (doc 02): the shell is registered with a null target in pass 1; the
		// real target is set once every type name exists, so forward refs to aliases resolve.
		void SetAliasedType(const Type* t) { m_AliasedType = t; }

	private:
		Core::String m_FullName;
		const Type* m_AliasedType = nullptr;
	};
} // namespace Scripting
