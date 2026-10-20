#pragma once

namespace llvm { class Function; class Module; class IntegerType; }

namespace Scripting
{
	/// `a ** b` on integers (D-26): one internal function per integer type, by square-and-multiply with
	/// wrapping multiplication. A negative exponent gives 0, except for a base of 1 (gives 1) and -1 (gives -1
	/// for an odd exponent, 1 for an even one).
	llvm::Function* IntegerPowerFunction(llvm::Module& module, llvm::IntegerType* type, bool isSigned);
}
