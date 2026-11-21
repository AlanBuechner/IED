#include "Script/ScriptCache.h"
#include "Compiler/FileIo.h"
#include "Script/ScriptCompiler.h"
#include "Script/ScriptReach.h"
#include <llvm/Support/BLAKE3.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Process.h>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Scripting
{
	namespace fs = std::filesystem;

	namespace
	{
		constexpr char kEntryMagic[] = "C4SCRIPT3";
		constexpr const char* kEntryExtension = ".c4so";

		// Each input goes in with its length, so no two lists of inputs hash alike.
		class Digest
		{
		public:
			void Add(llvm::StringRef text)
			{
				const uint64 length = text.size();
				m_Hasher.update(llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t*>(&length), sizeof(length)));
				m_Hasher.update(llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
			}

			std::string Hex()
			{
				const llvm::BLAKE3Result<16> sum = m_Hasher.final<16>();
				static constexpr char kDigits[] = "0123456789abcdef";
				std::string hex;
				for (uint8_t byte : sum) { hex += kDigits[byte >> 4]; hex += kDigits[byte & 15]; }
				return hex;
			}

		private:
			llvm::BLAKE3 m_Hasher;
		};

		bool ReadText(const std::string& path, std::string& text)
		{
			if (!FileExists(path)) return false;
			text = ReadFileBytes(path);
			return true;
		}

		// The name a script is cached under: its name without a trailing `#<tag>`, which a host adds to tell builds of
		// one script apart (each loads under its own reflection tag, which is not in the machine code).
		std::string CachedName(const std::string& name) { return name.substr(0, name.rfind('#')); }

		// The script's cached name as a file name: its letters, digits, '.', '_' and '-', anything else as '_'.
		std::string FileNameOf(const std::string& name)
		{
			std::string file = CachedName(name).empty() ? std::string("script") : CachedName(name);
			for (char& c : file)
				if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') c = '_';
			return file;
		}

		// "<name>.O2-", the start of every entry of one script at one level.
		std::string EntryPrefix(const ScriptCompileOptions& options) { return FileNameOf(options.name) + (options.optimize ? ".O2-" : ".O0-"); }

		void AddNumber(Digest& digest, uint64 value) { digest.Add(llvm::StringRef(reinterpret_cast<const char*>(&value), sizeof(value))); }

		void AddFile(Digest& digest, const InterfaceFile& file, InterfaceDigests& digests)
		{
			AddNumber(digest, file.unitId);
			AddNumber(digest, file.project);
			AddNumber(digest, static_cast<uint64>(static_cast<int64>(file.folder)));
			digest.Add(file.path);
			digest.Add(file.library ? "library" : file.prelude ? "prelude" : "source");
			digest.Add(file.binding ? "binding" : "own");
			for (uint32 binding : file.bindings) AddNumber(digest, binding);
			for (const MacroDefinition& macro : file.macros)
			{
				digest.Add(macro.name);
				digest.Add(macro.isFunctionLike ? "(" : "=");
				for (const std::string& param : macro.params) digest.Add(param);
				digest.Add(macro.body);
			}
			digest.Add("|");
			for (const std::string& name : file.templates) digest.Add(name);
			digest.Add("|");
			for (const std::string& name : file.extendedTemplates) digest.Add(name);
			digest.Add(digests.Of(file.unitId));
		}

		// What of the host a script's code follows from, beyond the names and bodies its entry checks (HostAssumptions):
		// the host's shape - its target, projects, how many files, which function symbols collided - and the files the
		// script's project reaches. A file out of that reach decides nothing of the script's code.
		void AddHost(Digest& digest, const HostInterface& host, InterfaceDigests& digests, const ScriptCompileOptions& options)
		{
			AddNumber(digest, host.compilerVersion);
			digest.Add(host.targetCpu);
			digest.Add(host.targetFeatures);
			for (const InterfaceProject& project : host.projects)
			{
				digest.Add(project.name);
				for (uint32 used : project.dependencies) AddNumber(digest, used);
				digest.Add("|");
				for (uint32 folder : project.folders) AddNumber(digest, folder);
				digest.Add("|");
			}
			AddNumber(digest, host.rootProject);
			AddNumber(digest, host.folderCount);
			AddNumber(digest, host.files.size());
			for (const CollidedSymbol& symbol : host.collidedSymbols)
			{
				digest.Add(symbol.symbol);
				for (uint32 unit : symbol.units) AddNumber(digest, unit);
			}
			digest.Add("|");
			const std::vector<const ScriptChain*> layers = LayersOf(options.base.get());
			for (uint32 unit : ReachedUnits(host, layers, ScriptDependencies(host, layers, options.allow, options.onlyAllowed)))
				if (unit >= 1 && unit <= host.files.size()) AddFile(digest, host.files[unit - 1], digests);
		}

		bool AddInputs(Digest& digest, const HostInterface& host, InterfaceDigests& digests, const ScriptCompileOptions& options)
		{
			AddHost(digest, host, digests, options);
			digest.Add(CachedName(options.name));
			digest.Add(options.optimize ? "O2" : "O0");
			for (const std::string& entry : options.allow) digest.Add(entry);
			digest.Add(options.onlyAllowed ? "|only" : "|");
			for (const auto& [name, value] : options.defines) { digest.Add(name); digest.Add(value); }
			digest.Add("|");
			digest.Add(options.project);
			for (const std::string& root : options.roots) digest.Add(fs::absolute(root).lexically_normal().generic_string());
			digest.Add(options.extensible ? "extensible" : "closed");
			digest.Add(options.base ? options.base->digest : std::string());
			digest.Add("|");
			for (const ScriptFile& file : options.files)
			{
				std::string text;
				if (file.text) text = *file.text;
				else if (!ReadText(file.path, text)) return false;
				digest.Add(file.text ? file.path : fs::absolute(file.path).lexically_normal().generic_string());
				digest.Add(text);
			}
			return true;
		}

		// --- the entry's bytes: the magic, then each part as its length and its bytes ----------------------------------

		void PutPart(std::string& out, llvm::StringRef part)
		{
			const uint64 length = part.size();
			out.append(reinterpret_cast<const char*>(&length), sizeof(length));
			out.append(part.data(), part.size());
		}

		bool TakePart(llvm::StringRef& in, std::string& part)
		{
			uint64 length = 0;
			if (in.size() < sizeof(length)) return false;
			std::memcpy(&length, in.data(), sizeof(length));
			in = in.drop_front(sizeof(length));
			if (in.size() < length) return false;
			part = in.take_front(length).str();
			in = in.drop_front(length);
			return true;
		}

		void PutCount(std::string& out, size_t count) { PutPart(out, std::to_string(count)); }

		bool TakeCount(llvm::StringRef& in, uint64& count)
		{
			std::string text;
			if (!TakePart(in, text)) return false;
			count = std::strtoull(text.c_str(), nullptr, 10);
			return true;
		}

		void PutAnswers(std::string& out, const std::vector<std::pair<std::string, bool>>& answers)
		{
			PutCount(out, answers.size());
			for (const auto& [name, held] : answers) PutPart(out, (held ? "+" : "-") + name);
		}

		bool TakeAnswers(llvm::StringRef& in, std::vector<std::pair<std::string, bool>>& answers)
		{
			uint64 count = 0;
			if (!TakeCount(in, count)) return false;
			for (uint64 i = 0; i < count; ++i)
			{
				std::string answer;
				if (!TakePart(in, answer) || answer.empty()) return false;
				answers.emplace_back(answer.substr(1), answer[0] == '+');
			}
			return true;
		}

		void PutAssumptions(std::string& out, const HostAssumptions& assumptions)
		{
			PutAnswers(out, assumptions.symbols);
			PutAnswers(out, assumptions.types);
			PutCount(out, assumptions.inlineBodies.size());
			for (const auto& [name, digest] : assumptions.inlineBodies)
			{
				PutPart(out, name);
				PutPart(out, digest);
			}
			PutPart(out, assumptions.wholeInterface);
		}

		bool TakeAssumptions(llvm::StringRef& in, HostAssumptions& assumptions)
		{
			uint64 bodies = 0;
			if (!TakeAnswers(in, assumptions.symbols) || !TakeAnswers(in, assumptions.types) || !TakeCount(in, bodies)) return false;
			for (uint64 i = 0; i < bodies; ++i)
			{
				auto& [name, digest] = assumptions.inlineBodies.emplace_back();
				if (!TakePart(in, name) || !TakePart(in, digest)) return false;
			}
			return TakePart(in, assumptions.wholeInterface);
		}

		std::string Encode(const CachedScript& script)
		{
			std::string out(kEntryMagic);
			PutPart(out, script.diagnostics);
			PutCount(out, script.threadLocals.size());
			for (const std::string& name : script.threadLocals) PutPart(out, name);
			PutPart(out, script.layer);
			PutAssumptions(out, script.assumptions);
			PutPart(out, script.object);
			return out;
		}

		std::optional<CachedScript> Decode(llvm::StringRef in)
		{
			if (!in.consume_front(kEntryMagic)) return std::nullopt;
			CachedScript script;
			uint64 locals = 0;
			if (!TakePart(in, script.diagnostics) || !TakeCount(in, locals)) return std::nullopt;
			for (uint64 i = 0; i < locals; ++i)
				if (!TakePart(in, script.threadLocals.emplace_back())) return std::nullopt;
			if (!TakePart(in, script.layer) || !TakeAssumptions(in, script.assumptions)) return std::nullopt;
			if (!TakePart(in, script.object) || !in.empty() || script.object.empty()) return std::nullopt;
			return script;
		}

		void RemoveOthers(const std::string& folder, const std::string& prefix, const std::string& kept)
		{
			std::error_code ec;
			for (llvm::sys::fs::directory_iterator entry(folder, ec), end; !ec && entry != end; entry.increment(ec))
			{
				const fs::path path(entry->path());
				const std::string name = path.filename().string();
				if (name != kept && name.rfind(prefix, 0) == 0 && path.extension() == kEntryExtension) RemoveFile(entry->path());
			}
		}
	}

	std::string LayerDigest(const std::string& below, const std::string& serializedLayer)
	{
		Digest digest;
		digest.Add(below);
		digest.Add(serializedLayer);
		return digest.Hex();
	}

	std::string ScriptCache::EntryFor(const HostInterface& host, InterfaceDigests& digests, const ScriptCompileOptions& options) const
	{
		Digest digest;
		digest.Add(kEntryMagic);
		digest.Add(m_Compiler);
		if (!AddInputs(digest, host, digests, options)) return "";
		return (fs::path(m_Folder) / (EntryPrefix(options) + digest.Hex() + kEntryExtension)).string();
	}

	std::optional<CachedScript> ScriptCache::Read(const std::string& entry) const
	{
		auto bytes = llvm::MemoryBuffer::getFile(entry, /*IsText=*/false, /*RequiresNullTerminator=*/false);
		if (!bytes) return std::nullopt;
		return Decode((*bytes)->getBuffer());
	}

	void ScriptCache::Write(const std::string& entry, const CachedScript& script) const
	{
		CreateFolders(m_Folder);
		// Written aside and renamed into place, so a reader never sees half an entry. Through LLVM's file calls, which
		// take paths longer than MAX_PATH (a deep build folder).
		const std::string written = entry + ".tmp" + std::to_string(llvm::sys::Process::getProcessId()) + "-" +
			std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id()));
		if (!WriteFileBytes(written, Encode(script))) return;
		if (RenameFile(written, entry)) { RemoveFile(written); return; }
		const std::string name = fs::path(entry).filename().string();
		RemoveOthers(m_Folder, name.substr(0, name.rfind('-') + 1), name); // the digest holds no '-'
	}

	std::string ScriptCompilerIdentity()
	{
#if !defined(_WIN32)
		// The shared library (or program) this code is in, by its file, size and time.
		Dl_info info = {};
		if (!dladdr(reinterpret_cast<void*>(&ScriptCompilerIdentity), &info) || !info.dli_fname || !*info.dli_fname) return "";
		std::error_code ec;
		const fs::path file = fs::canonical(info.dli_fname, ec);
		if (ec) return "";
		const std::optional<FileStamp> stamp = StampOf(PathText(file));
		if (!stamp) return "";
		return PathText(file) + "|" + std::to_string(stamp->size) + "|" + std::to_string(stamp->modified);
#else
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&ScriptCompilerIdentity), &module))
			return "";
		wchar_t path[MAX_PATH * 4];
		const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
		if (length == 0 || length >= std::size(path)) return "";
		const std::optional<FileStamp> stamp = StampOf(PathText(fs::path(std::wstring(path, length))));
		if (!stamp) return "";
		return std::string(reinterpret_cast<const char*>(path), length * sizeof(wchar_t)) + "|" + std::to_string(stamp->size) + "|" +
			std::to_string(stamp->modified);
#endif
	}
} // namespace Scripting
