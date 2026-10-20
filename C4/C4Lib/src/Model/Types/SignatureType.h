#pragma once
#include "Model/Types/Type.h"

namespace Scripting
{
	/// A type that is called with a signature: `funcptr<R(A)>` (FunPtrType) and `func<R(A)>` (FuncType). Code
	/// that only needs the signature works on this; code for one of the two checks the kind.
	class SignatureType : public Type
	{
	public:
		const Type* GetReturnType() const { return m_ReturnType; }
		const Core::Vector<const Type*>& GetParameterTypes() const { return m_ParameterTypes; }
		uint32 GetParameterCount() const { return m_ParameterTypes.Count(); }

		Core::String GetName() const override { return ToString(); }
		Core::String ToString() const override;

		/// Whether `ret(params)` is this signature (aliases are transparent).
		bool HasSignature(const Type* ret, const Core::Vector<const Type*>& params) const;

	protected:
		SignatureType(TypeKind kind, const char* former, const Type* returnType, const Core::Vector<const Type*>& paramTypes);

	private:
		const char* m_Former;
		const Type* m_ReturnType = nullptr;
		Core::Vector<const Type*> m_ParameterTypes;
	};
} // namespace Scripting
