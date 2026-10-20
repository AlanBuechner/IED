#pragma once
#include "Target/Process.h"
#include <string>

namespace C4DAP::Target
{
	/// The exception code of a C++ (and so a C4) throw.
	constexpr DWORD kCppExceptionCode = 0xE06D7363;

	/// The C4 name of a type's MSVC type descriptor name: ".?AVBadCast@@" is "BadCast", ".?AUPoint@Geo@@" is
	/// "Geo::Point". A name it cannot read (a template's, a primitive's) comes back as it is.
	std::string TypeDescriptorName(const std::string& decorated);

	/// What a C++ throw threw, from its exception record and the throw information it points at; "" when unreadable.
	std::string ThrownType(const Process& process, const EXCEPTION_RECORD& record);
} // namespace C4DAP::Target
