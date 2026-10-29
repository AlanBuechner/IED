#pragma once
#include "Core/String.h"
#include "Core/Vector.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Scripting
{
	/// A foreign C/C++ source file that must be compiled and linked into the final binary.
	struct ForeignObject
	{
		std::string srcPath;
		std::string objPath;
		std::string language = "c";
		std::vector<std::string> defines;
		std::vector<std::string> includeDirs;
		std::vector<std::string> targetFlags; // the build's -march / -m flags (D-62)
		std::vector<std::string> compileFlags; // the build's -O and -g (BUG-285)
	};

	/// How CBind parses a foreign header: what its bindings are cached by.
	struct BindingFlags
	{
		std::string language;
		std::vector<std::string> defines;
		std::vector<std::string> includeDirs;
		std::vector<std::string> targetFlags; // the build's, so the header sees the same processor macros
	};

	/// One file handed to the interop pass: its display name (for diagnostics), unique identity,
	/// and macro-expanded text. The identity (normalized absolute path) is what callers match results
	/// back to sources on - a bare display name is only unique within one root folder.
	struct InteropFileInput
	{
		std::string name;    // display name (relative path), for diagnostics
		std::string absPath; // normalized absolute path - the file's unique identity across roots
		std::string text;    // macro-expanded source
		std::vector<std::string> folders; // where its headers are found after its own folder (its project's and its dependencies', D-59); empty: the root folders
	};

	/// One interop transformation result, keyed by the source's unique identity.
	struct InteropFileResult
	{
		std::string name;    // display name (relative path), matches the input
		std::string absPath; // normalized absolute path - the file's unique identity across roots
		std::string text;    // transformed source (bindings appended)
		// Byte offset, in the input (macro-expanded) text, of the `extern ... include` directive
		// whose bindings were appended. Lets the LSP map a go-to-definition on a generated symbol
		// back to the directive line the user can actually see. SIZE_MAX when none was appended.
		size_t directiveOffset = SIZE_MAX;
		// The line (1-based) the appended bindings start on: every declaration from it on is a binding's (BUG-420).
		uint32_t bindingsLine = 0;
	};

	/// Runs the C/C++ interop pre-pass over a set of macro-expanded C4 sources.
	///
	/// Scans each file's text for `extern "c"|"c++" include|source "path" [ { config } ]`
	/// directives. For `include`, it invokes the CBind tool to generate C4 declaration text
	/// from the foreign header and appends it to the file (codegen dedupes types by name across
	/// .cf files, so identical bindings in multiple files are safe). For `source`, it compiles
	/// the foreign file into an object to be linked. Config blocks support define("N","V"),
	/// include_dir("D"), and lib("path") / lib("name.lib") - import libraries appended to the
	/// final link (used for Win32/DX12 apps that call user32/d3d12/etc. through a shim).
	class InteropPass
	{
	public:
		struct Result
		{
			std::vector<InteropFileResult> files;   // transformed texts (only files that changed)
			std::vector<ForeignObject> objects;     // foreign sources to compile + link
			std::vector<std::string> libraries;     // import libraries (.lib paths or names) to link
			// Hard failures in the interop pre-pass (unresolvable extern path, CBind rejected the
			// header, a foreign source failed to compile). A non-empty list means the build must not
			// produce a binary - the driver turns these into hard errors.
			std::vector<std::string> errors;
		};

		/// Process every file in `macroResults` (name -> macro-expanded text). `rootFolder` is the
		/// source root for include resolution; `cliIncludeDirs` are extra -I dirs; `objDir` is where
		/// foreign objects and generated bindings are written. Returns an empty Result if no interop
		/// directives exist or CBind is unavailable (a warning is printed in that case).
		/// When `declarationsOnly` is true, only binding text is generated: foreign sources and
		/// vtable shims are not compiled into objects (used by the LSP, which needs declarations
		/// for analysis but never links or runs code).
		/// `targetFlags` are the build's -march / -m flags: headers are parsed and sources compiled for its processor.
		static Result RunFolder(const std::vector<std::string>& rootFolders,
			const std::vector<InteropFileInput>& files,
			const std::vector<std::string>& cliIncludeDirs,
			const std::string& objDir, const std::vector<std::string>& targetFlags, bool verbose, bool declarationsOnly = false,
			const std::vector<std::string>& compileFlags = {});

	private:
		static bool ResolveForeignHeader(const std::string& headerPath, const std::string& fileDir,
			const std::vector<std::string>& rootFolders, const std::vector<std::string>& cliIncludeDirs, std::string& outAbs);
		static bool GenerateBindings(const std::string& cbindExe, const std::string& headerAbs, const BindingFlags& flags,
			const std::string& outFile, bool verbose);
		static bool CompileForeign(const ForeignObject& fo, bool verbose);
	};
}
