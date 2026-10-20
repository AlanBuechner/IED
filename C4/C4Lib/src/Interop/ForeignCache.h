#pragma once
#include "Interop/InteropPass.h"
#include <string>

namespace Scripting
{
	/// Whether `fo`'s object is still what compiling it now would give (BUG-285): the object, its depfile (`<obj>.d`,
	/// which clang writes) and its key (`<obj>.key`) are there, and the key of the source's bytes, the flags, the
	/// compiler and the bytes of every header the depfile lists is the one recorded.
	bool ForeignObjectIsCurrent(const ForeignObject& fo, const std::string& compiler);

	/// Records the key of the object just compiled for `fo` (its depfile written by that compile).
	void RecordForeignObject(const ForeignObject& fo, const std::string& compiler);

	/// The files a Make depfile says its target depends on (`obj: a.c b.h \`), spaces unescaped.
	std::vector<std::string> DepfileInputs(const std::string& text);
}
