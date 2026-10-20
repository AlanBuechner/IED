#pragma once
#include "Model/Types/Type.h"

namespace Scripting
{
	/// A reference type: `T&`. Size is pointer-sized (mirrors PointerType).
	class ReferenceType : public Type
	{
	public:
		explicit ReferenceType(const Type* referentType, bool constReferent = false);
		Core::String GetName() const override;
		Core::String ToString() const override;
		const Type* GetReferentType() const { return m_ReferentType; }
		bool IsConstReferent() const { return m_ConstReferent; }

	private:
		const Type* m_ReferentType = nullptr;
		bool m_ConstReferent = false; // `const T&` - the referent is read-only (mirrors PointerType)
	};
} // namespace Scripting
