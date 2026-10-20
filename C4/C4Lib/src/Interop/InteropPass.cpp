#include "InteropPass.h"
#include "Interop/ForeignCache.h"
#include "Compiler/BuildUtils.h"
#include "Compiler/RuntimeObjects.h"
#include "Compiler/Toolchain.h"
#include "Core/Core.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace Scripting
{
	namespace
	{
		std::string Norm(const std::string& p)
		{
			std::string r = p;
			std::replace(r.begin(), r.end(), '\\', '/');
			return r;
		}

		std::string ReadFileText(const fs::path& path)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f.is_open()) return "";
			std::ostringstream ss;
			ss << f.rdbuf();
			return ss.str();
		}

		// A binding's macro constants (BUG-284) that an earlier binding in the same file already declared, left out: two
		// headers that both include <stdint.h> each bind INT32_MAX. Constants follow CBind's marker, one per line as
		// `const <type> <name> = <value>;`.
		std::string WithoutRepeatedConstants(const std::string& binding, std::unordered_set<std::string>& declared)
		{
			std::istringstream lines(binding);
			std::string out;
			std::string line;
			bool constants = false;
			while (std::getline(lines, line))
			{
				if (line.rfind("// Constants from the header's object-like macros", 0) == 0) constants = true;
				else if (constants && line.rfind("const ", 0) == 0)
				{
					std::istringstream words(line);
					std::string keyword, type, name;
					words >> keyword >> type >> name;
					if (!declared.insert(name).second) continue;
				}
				out += line + "\n";
			}
			return out;
		}

		std::string FindCBindExe()
		{
			const fs::path cbind = Toolchain::CBind();
			return cbind.empty() ? std::string() : Norm(cbind.string());
		}

		struct FileStamp { bool exists = false; uintmax_t mtime = 0; uintmax_t size = 0; };
		FileStamp Stamp(const std::string& path)
		{
			FileStamp s;
			std::error_code ec;
			if (!fs::exists(path, ec)) return s;
			s.exists = true;
			s.mtime = fs::last_write_time(path, ec).time_since_epoch().count();
			s.size = fs::file_size(path, ec);
			return s;
		}

		// A binding is reusable when the header and CBind.exe are unchanged since it was written.
		// This turns the LSP's per-recompile interop pass from "CBind every file every time"
		// (~minutes for SDK headers) into a cache hit, without ever serving stale bindings: any
		// edit to the header or a rebuild of CBind changes one of the stamps.
		bool BindingIsCached(const std::string& outFile, const std::string& headerAbs, const std::string& cbindExe, const BindingFlags& flags)
		{
			FileStamp h = Stamp(headerAbs), cb = Stamp(cbindExe);
			if (!h.exists || !cb.exists) return false;

			std::ifstream f(outFile + ".cache");
			if (!f.is_open()) return false;

			uintmax_t hm, hs, cm, cs;
			std::string l;
			if (!(f >> hm >> hs >> cm >> cs)) return false;
			std::getline(f, l); // trailing \n after the stamp line
			std::getline(f, l);
			if (l != flags.language) return false;

			auto readCountedList = [&](std::vector<std::string>& out) -> bool {
				out.clear();
				int count = 0;
				if (!(f >> count) || count < 0) return false;
				std::getline(f, l); // rest of the count line
				for (int k = 0; k < count; ++k)
				{
					if (!std::getline(f, l)) return false;
					out.push_back(l);
				}
				return true;
			};
			std::vector<std::string> d, inc, target;
			if (!readCountedList(d) || !readCountedList(inc) || !readCountedList(target)) return false;

			return hm == h.mtime && hs == h.size && cm == cb.mtime && cs == cb.size && d == flags.defines && inc == flags.includeDirs &&
				target == flags.targetFlags;
		}

		void WriteCountedList(std::ofstream& f, const std::vector<std::string>& list)
		{
			f << list.size() << "\n";
			for (const auto& entry : list) f << entry << "\n";
		}

		void WriteBindingCache(const std::string& outFile, const std::string& headerAbs, const std::string& cbindExe, const BindingFlags& flags)
		{
			FileStamp h = Stamp(headerAbs), cb = Stamp(cbindExe);
			std::ofstream f(outFile + ".cache", std::ios::binary | std::ios::trunc);
			if (!f.is_open()) return;
			f << h.mtime << " " << h.size << " " << cb.mtime << " " << cb.size << "\n" << flags.language << "\n";
			WriteCountedList(f, flags.defines);
			WriteCountedList(f, flags.includeDirs);
			WriteCountedList(f, flags.targetFlags);
		}

		// Case-insensitive whole-word match for a keyword at position i (bounded by non-identifier chars).
		bool KeywordAt(const std::string& s, size_t i, const char* kw)
		{
			size_t len = strlen(kw);
			if (s.size() - i < len) return false;
			for (size_t k = 0; k < len; ++k)
				if (tolower((unsigned char)s[i + k]) != tolower((unsigned char)kw[k])) return false;
			// word boundary checks
			if (i > 0 && isalnum((unsigned char)s[i - 1])) return false;
			size_t end = i + len;
			if (end < s.size() && isalnum((unsigned char)s[end])) return false;
			return true;
		}

		// Read a double-quoted string starting at s[i] == '"'. Returns content, advances i past closing quote.
		bool ReadQuoted(const std::string& s, size_t& i, std::string& out)
		{
			if (i >= s.size() || s[i] != '"') return false;
			++i;
			out.clear();
			while (i < s.size() && s[i] != '"')
			{
				if (s[i] == '\\' && i + 1 < s.size()) { ++i; }
				out += s[i];
				++i;
			}
			if (i >= s.size()) return false; // unterminated
			++i; // closing quote
			return true;
		}

		struct ExternDirective
		{
			size_t offset = 0; // byte offset of the 'extern' keyword in the input text
			std::string language = "c";
			bool isInclude = true;
			std::string path;
			std::vector<std::string> defines;
			std::vector<std::string> includeDirs;
			std::vector<std::string> libraries;
		};

		// Parse one extern directive starting at the 'extern' keyword (s[i]..). Returns false if not an extern.
		bool ParseExternAt(const std::string& s, size_t i, ExternDirective& out)
		{
			size_t p = i;
			while (p < s.size() && isspace((unsigned char)s[p])) ++p;
			if (!KeywordAt(s, p, "extern")) return false;
			p += 6;
			while (p < s.size() && isspace((unsigned char)s[p])) ++p;

			std::string lang;
			if (!ReadQuoted(s, p, lang)) return false;
			out.language = lang;
			while (p < s.size() && isspace((unsigned char)s[p])) ++p;

			bool isInc = KeywordAt(s, p, "include");
			bool isSrc = KeywordAt(s, p, "source");
			if (!isInc && !isSrc) return false;
			out.isInclude = isInc;
			p += isInc ? 7 : 6;
			while (p < s.size() && isspace((unsigned char)s[p])) ++p;

			std::string path;
			if (!ReadQuoted(s, p, path)) return false;
			out.path = path;

			// Optional config block { define("N","V"); include_dir("D"); ... }
			while (p < s.size() && isspace((unsigned char)s[p])) ++p;
			if (p < s.size() && s[p] == '{')
			{
				++p;
				while (p < s.size() && s[p] != '}')
				{
					while (p < s.size() && isspace((unsigned char)s[p])) ++p;
					if (s[p] == '}') break;

					size_t nameStart = p;
					while (p < s.size() && (isalnum((unsigned char)s[p]) || s[p] == '_')) ++p;
					std::string directive = s.substr(nameStart, p - nameStart);
					while (p < s.size() && isspace((unsigned char)s[p])) ++p;
					if (p >= s.size() || s[p] != '(') break;
					++p;

					std::string a1;
					if (!ReadQuoted(s, p, a1)) break;
					if (directive == "define")
					{
						while (p < s.size() && isspace((unsigned char)s[p])) ++p;
						std::string a2;
						if (p < s.size() && s[p] == ',') { ++p; ReadQuoted(s, p, a2); out.defines.push_back(a1 + "=" + a2); }
						else out.defines.push_back(a1);
					}
					else if (directive == "include_dir")
					{
						out.includeDirs.push_back(a1);
					}
					else if (directive == "lib")
					{
						out.libraries.push_back(a1);
					}

					while (p < s.size() && isspace((unsigned char)s[p])) ++p;
					if (p < s.size() && s[p] == ')') ++p;
					while (p < s.size() && isspace((unsigned char)s[p])) ++p;
					if (p < s.size() && s[p] == ';') ++p;
				}
				if (p < s.size() && s[p] == '}') ++p;
			}
			return true;
		}

		// Find all extern directives in text, skipping comment and string-literal regions so a
		// doc-comment that merely *mentions* `extern "c++" include "<headers>"` is not treated as
		// a real directive. Records the offset of each 'extern' keyword for LSP coordinate mapping.
		std::vector<ExternDirective> FindExternDirectives(const std::string& text)
		{
			std::vector<ExternDirective> found;
			size_t i = 0, n = text.size();
			while (i < n)
			{
				char c = text[i];

				if (c == '/' && i + 1 < n && text[i + 1] == '/') // line comment
				{
					size_t e = text.find('\n', i);
					i = (e == std::string::npos) ? n : e + 1;
					continue;
				}
				if (c == '/' && i + 1 < n && text[i + 1] == '*') // block comment
				{
					size_t e = text.find("*/", i + 2);
					i = (e == std::string::npos) ? n : e + 2;
					continue;
				}
				if (c == '"') // string literal: skip past its closing quote
				{
					++i;
					while (i < n && text[i] != '"')
						i = (text[i] == '\\' && i + 1 < n) ? i + 2 : i + 1;
					if (i < n) ++i;
					continue;
				}

				if (KeywordAt(text, i, "extern"))
				{
					ExternDirective d;
					d.offset = i;
					if (ParseExternAt(text, i, d))
						found.push_back(d);
				}
				++i;
			}
			return found;
		}

		std::string ResolveHeader(const std::string& headerPath, const std::string& fileDir,
			const std::vector<std::string>& rootFolders, const std::vector<std::string>& cliIncludeDirs)
		{
			std::string hp = Norm(headerPath);
			bool isAbs = (hp.size() >= 2 && hp[1] == ':') || (hp.front() == '/');
			if (isAbs && fs::exists(hp)) return hp;

			std::vector<std::string> candidates;
			candidates.push_back(Norm(fileDir + "/" + hp));
			for (const auto& root : rootFolders)
				candidates.push_back(Norm(root + "/" + hp));
			for (const auto& inc : cliIncludeDirs)
				candidates.push_back(Norm(inc + "/" + hp));

			for (const auto& c : candidates)
				if (fs::exists(c)) return c;
			return "";
		}

		// Resolve a bare import-library name (e.g. "user32.lib") against the installed Windows SDK:
		// pick the newest 10.x version and its x64 um/lib folder. Returns "" when not found there;
		// callers fall back to passing the name through for the linker's own search path.
		std::string ResolveImportLibrary(const std::string& libName)
		{
			if (libName.find('/') != std::string::npos || libName.find('\\') != std::string::npos ||
				(libName.size() >= 2 && libName[1] == ':'))
				return Norm(libName); // already a path

			std::string kitsRoot = "C:/Program Files (x86)/Windows Kits/10/Lib";
			std::vector<std::string> versions;
			if (fs::exists(kitsRoot))
				for (const auto& entry : fs::directory_iterator(kitsRoot))
					if (entry.is_directory()) versions.push_back(entry.path().filename().string());
			if (versions.empty()) return "";
			std::sort(versions.rbegin(), versions.rend()); // newest first

			for (const auto& v : versions)
			{
				for (const char* sub : { "um/x64", "um/x86" })
				{
					fs::path candidate = fs::path(kitsRoot) / v / sub / libName;
					if (fs::exists(candidate)) return Norm(candidate.string());
				}
			}
			return "";
		}
	}

		// What an intermediate made from a foreign file is named after: a hash of the file's absolute path, then its
		// name. Same-named files in different folders, and `util.c` beside `util.cpp`, must not share an output.
		std::string PathTag(const std::string& absPath)
		{
			char buf[24];
			snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)std::hash<std::string>{}(absPath));
			return std::string(buf) + "_" + fs::path(absPath).filename().string();
		}

	bool InteropPass::ResolveForeignHeader(const std::string& headerPath, const std::string& fileDir,
		const std::vector<std::string>& rootFolders, const std::vector<std::string>& cliIncludeDirs, std::string& outAbs)
	{
		outAbs = ResolveHeader(headerPath, fileDir, rootFolders, cliIncludeDirs);
		return !outAbs.empty();
	}

	bool InteropPass::GenerateBindings(const std::string& cbindExe, const std::string& headerAbs, const BindingFlags& flags,
		const std::string& outFile, bool verbose)
	{
		Core::Vector<Core::String> args;
		args.Push(cbindExe);
		args.Push(headerAbs);
		args.Push(outFile);
		args.Push("--lang");
		args.Push(flags.language);
		for (const auto& d : flags.defines) { Core::String f("-D"); f += d; args.Push(f); }
		for (const auto& inc : flags.includeDirs) { args.Push("-I"); args.Push(inc); }
		for (const auto& target : flags.targetFlags) args.Push(target);

		int code = -1;
		std::string so, se;
		if (!RunToolWithOutput(args, code, so, se))
		{
			CORE_WARN("Interop: failed to launch CBind for {0}: {1}", headerAbs, se);
			return false;
		}
		if (code != 0)
		{
			CORE_WARN("Interop: CBind failed on {0}:\n{1}", headerAbs, se);
			return false;
		}
		std::string text = ReadFileText(outFile);
		if (text.empty())
		{
			CORE_WARN("Interop: CBind produced no bindings for {0}", headerAbs);
			return false;
		}
		if (verbose)
			CORE_INFO("Interop: generated bindings for {0}", headerAbs);
		return true;
	}

	bool InteropPass::CompileForeign(const ForeignObject& fo, bool verbose)
	{
		if (!fs::exists(fo.srcPath))
		{
			CORE_WARN("Interop: foreign source not found: {0}", fo.srcPath);
			return false;
		}
		Core::Vector<Core::String> defines, includeDirs;
		for (const auto& d : fo.defines) defines.Push(d);
		for (const auto& inc : fo.includeDirs) includeDirs.Push(inc);
		includeDirs.Push(Core::String(RuntimeFolder().c_str())); // native code registers with the runtime through its C API (ReflectABI.h)
		// An object whose source, headers and flags are unchanged is kept (BUG-285).
		const std::string compiler = ClangPath();
		if (ForeignObjectIsCurrent(fo, compiler))
		{
			if (verbose) CORE_INFO("Interop: {0} is up to date", fo.srcPath);
			return true;
		}
		if (!CompileForeignSource(fo.srcPath, fo.objPath, fo.language, defines, includeDirs, fo.targetFlags, fo.compileFlags, verbose))
		{
			CORE_WARN("Interop: failed to compile foreign source {0}", fo.srcPath);
			return false;
		}
		RecordForeignObject(fo, compiler);
		return true;
	}

	InteropPass::Result InteropPass::RunFolder(const std::vector<std::string>& rootFolders,
		const std::vector<InteropFileInput>& files,
		const std::vector<std::string>& cliIncludeDirs, const std::string& objDir, const std::vector<std::string>& targetFlags, bool verbose,
		bool declarationsOnly, const std::vector<std::string>& compileFlags)
	{
		Result result;
		std::string cbindExe = FindCBindExe();

		for (const auto& f : files)
		{
			const std::string& name = f.name;
			const std::string& text = f.text;
			auto directives = FindExternDirectives(text);
			if (directives.empty()) continue;

			std::string fileDir = Norm(fs::path(f.absPath).parent_path().string());

			std::string transformed = text;
			std::unordered_set<std::string> boundConstants;
			bool changed = false;
			size_t dirOffset = SIZE_MAX; // offset of the first directive whose bindings were appended

			for (const auto& d : directives)
			{
			std::string absPath;
			// Resolve the main header against file dir, root folder, CLI include dirs, AND the
			// directive's own config-block include_dir entries (so a bare "windows.h" resolves
			// when the SDK path is given in the config block rather than on the CLI).
			std::vector<std::string> allInc = cliIncludeDirs;
			for (const auto& inc : d.includeDirs) allInc.push_back(inc);
			if (!ResolveForeignHeader(d.path, fileDir, f.folders.empty() ? rootFolders : f.folders, allInc, absPath))
			{
				CORE_WARN("Interop: cannot resolve extern path '{0}' in {1}", d.path, name);
				result.errors.push_back(
					"Cannot resolve extern path '" + d.path + "' referenced in " + name +
					". Check the path and any include_dir(\"...\") entries.");
				continue;
			}

			if (d.isInclude)
			{
				if (cbindExe.empty())
				{
					CORE_WARN("Interop: CBind.exe not found - skipping bindings for {0}", absPath);
					result.errors.push_back(
						"CBind.exe was not found, so no bindings could be generated for '" + d.path +
						"' referenced in " + name + ". Install the CBind tool next to the compiler.");
					continue;
				}
				std::string outFile = Norm(objDir + "/" + PathTag(absPath) + ".c4bind");
				bool haveBindings = false;
				const BindingFlags flags{ d.language, d.defines, d.includeDirs, targetFlags };
				if (fs::exists(outFile) && BindingIsCached(outFile, absPath, cbindExe, flags))
				{
					if (verbose) CORE_INFO("Interop: using cached bindings for {0}", absPath);
					haveBindings = true;
				}
				else if (GenerateBindings(cbindExe, absPath, flags, outFile, verbose))
				{
					WriteBindingCache(outFile, absPath, cbindExe, flags);
					haveBindings = true;
				}
				else
				{
					// CBind rejected the header (syntax error, unknown type, ...). The exact Clang
					// diagnostics were already printed by GenerateBindings; record a hard failure so
					// the build does not silently proceed with missing declarations.
					result.errors.push_back(
						"C/C++ interop failed for '" + d.path + "' referenced in " + name +
						" (see the CBind diagnostics above).");
				}
					if (haveBindings)
				{
					// Append: file-scope declarations are order independent (docs/language/01 1.7), and after the
					// user's text no user line moves, so diagnostics keep their lines. Record the first bound
					// directive's offset so the LSP can map F12 on a generated symbol to it.
					transformed = transformed + "\n" + WithoutRepeatedConstants(ReadFileText(outFile), boundConstants);
					if (dirOffset == SIZE_MAX) dirOffset = d.offset;
					changed = true;

					// C++ virtual dispatch: CBind emits a companion shim (one vtable trampoline per
					// virtual method). Compile it as a foreign object and link it in so [link]
					// references to the trampolines resolve.
					// A C header's shim is C: it forwards the header's static and inline functions (BUG-283).
					const fs::path shimStem = fs::path(outFile).parent_path() / fs::path(outFile).stem();
					const bool cxxShim = fs::exists(shimStem.string() + ".cbind_shim.cpp");
					const fs::path shimPath = shimStem.string() + (cxxShim ? ".cbind_shim.cpp" : ".cbind_shim.c");
					if (!declarationsOnly && fs::exists(shimPath))
					{
						ForeignObject fo;
						fo.srcPath = Norm(shimPath.string());
						fo.objPath = Norm(objDir + "/" + PathTag(absPath) + ".shim.obj");
						fo.language = cxxShim ? "c++" : "c";
						fo.defines = d.defines;
						fo.targetFlags = targetFlags;
						fo.compileFlags = compileFlags;
						// The shim #includes the header by filename - make sure its directory is on
						// the include path even when no explicit include_dirs were given.
						std::string headerDir = Norm(fs::path(absPath).parent_path().string());
						if (!headerDir.empty() && headerDir != ".")
						{
							bool have = false;
							for (const auto& inc : d.includeDirs) if (Norm(inc) == headerDir) { have = true; break; }
							if (!have) fo.includeDirs.push_back(headerDir);
						}
						fo.includeDirs.insert(fo.includeDirs.end(), d.includeDirs.begin(), d.includeDirs.end());
						if (!CompileForeign(fo, verbose))
							result.errors.push_back(
								"Failed to compile the generated shim for '" + d.path +
								"' referenced in " + name + ".");
						if (fs::exists(fo.objPath))
							result.objects.push_back(fo);
					}
				}
			}
				else // source
				{
					ForeignObject fo;
					fo.srcPath = absPath;
					fo.objPath = Norm(objDir + "/" + PathTag(absPath) + ".obj");
					fo.language = d.language;
					fo.defines = d.defines;
					fo.includeDirs = d.includeDirs;
					fo.targetFlags = targetFlags;
					fo.compileFlags = compileFlags;
					if (!declarationsOnly)
					{
						if (!CompileForeign(fo, verbose))
							result.errors.push_back(
								"Failed to compile foreign C/C++ source '" + d.path + "' referenced in " + name +
								" (see the compiler diagnostics above).");
					}
					if (fs::exists(fo.objPath))
						result.objects.push_back(fo);
				}
			}

		// Collect import libraries requested via lib("...") config directives. Bare names are
		// resolved against the installed Windows SDK; paths and unresolved names pass through so
		// the linker's own search path can still find them.
		for (const auto& d : directives)
			for (const auto& lib : d.libraries)
			{
				std::string resolved = ResolveImportLibrary(lib);
				if (resolved.empty()) resolved = Norm(lib);
				bool have = false;
				for (const auto& existing : result.libraries) if (existing == resolved) { have = true; break; }
				if (!have) result.libraries.push_back(resolved);
			}

		if (changed)
			result.files.push_back({ name, f.absPath, transformed, dirOffset });
		}

		return result;
	}
}
