#pragma once
#include "Model/Types/Type.h"

namespace C4Compiler
{
	/// The invalid/unknown type sentinel. Has no size or name beyond "<invalid>".
	class InvalidType : public Type
	{
	public:
		InvalidType();
		Core::String GetName() const override { return "<invalid>"; }
		Core::String ToString() const override { return "<invalid>"; }
	};
} // namespace C4Compiler
