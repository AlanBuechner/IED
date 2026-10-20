#pragma once
#include "Compiler/Projects.h"

namespace Scripting
{
	/// A build description as read, or why it could not be: `line N: ...`.
	struct ProjectFileResult
	{
		ProjectList list;
		std::string error;
	};

	/// Reads the build description a build tool writes (docs/language/15-projects.md), a subset of TOML: `#` comments,
	/// `key = "string"`, `key = ["string", ...]`, `[[project]]` and `[project.defines]`. Relative paths are relative to
	/// `folder`.
	ProjectFileResult ParseProjectFile(const std::string& text, const fs::path& folder);

	/// The description in `file`; an error names the file.
	ProjectFileResult ReadProjectFile(const fs::path& file);
}
