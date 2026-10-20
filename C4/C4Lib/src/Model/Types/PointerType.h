#pragma once
#include "Model/Types/Type.h"

namespace Scripting
{
	/// A pointer type: `T*`. Every C4 pointer is managed and is a PointerType. Size is pointer-sized.
	class PointerType : public Type
	{
	public:
		explicit PointerType(const Type* pointeeType, bool constReferent = false);
		Core::String GetName() const override;
		Core::String ToString() const override;
		const Type* GetPointeeType() const { return m_PointeeType; }
		bool IsConstReferent() const { return m_ConstReferent; }

	private:
		const Type* m_PointeeType = nullptr;
		bool m_ConstReferent = false; // `const T*` - the pointee is read-only (spec: leading const applies to the base type)
	};
} // namespace Scripting
