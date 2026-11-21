#pragma once
#include "Interop/InteropPass.h"
#include "SourceManager/SourceManager.h"
#include <vector>

namespace Scripting
{
	/// The registered files the interop pass reads: each whose text, as its parse reads it, may hold an `extern "c"` or
	/// `extern "c++"` directive. `projectFolders`: a file's headers are found in its project's folders and its
	/// dependencies' (a described build, D-59); else in the root folders.
	std::vector<InteropFileInput> InteropInputsOf(const SourceManager& sources, bool projectFolders);

	/// The binding units the interop pass made, registered: each once, and each file that binds one includes it (R8).
	void RegisterBindings(SourceManager& sources, const std::vector<InteropFileResult>& files);
}
