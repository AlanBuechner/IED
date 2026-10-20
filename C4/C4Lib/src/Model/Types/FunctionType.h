#pragma once
#include "Model/Types/Type.h"

namespace Scripting
{
	/// A function (or method) signature: return type + parameters. Size is pointer-sized (an address).
	class FunctionType : public Type
	{
	public:
		struct ParameterInfo
		{
			Core::String mName;
			const Type* m_Type = nullptr;
			mutable bool m_IsVariadic = false;
		};

		FunctionType(const Type* returnType, const Core::Vector<ParameterInfo>& params);
		Core::String GetName() const override;
		Core::String ToString() const override;

		const Type* GetReturnType() const { return m_ReturnType; }
		const Core::Vector<ParameterInfo>& GetParameters() const { return m_Parameters; }
		uint32 GetParameterCount() const { return m_Parameters.Count(); }
		bool IsVariadic() const { return m_IsVariadic; }
		void SetVariadic(bool variadic) const { m_IsVariadic = variadic; }

		// A stable, symbol-safe token derived from the parameter types (and the variadic flag). Appended to a
		// function's mangled name so OVERLOADS get distinct symbols: `Construct()` vs `Construct(int32)` must not
		// both emit @Construct. Derived purely from the signature (not names), so a declaration and its definition
		// - which dedupe by FunctionType pointer - always produce the SAME suffix and thus the same symbol.
		Core::String OverloadSuffix() const;

	private:
		const Type* m_ReturnType = nullptr;
		Core::Vector<ParameterInfo> m_Parameters;
		mutable bool m_IsVariadic = false;
	};
} // namespace Scripting
