#include "Script/ScriptCache.h"
#include "Compiler/FileIo.h"
#include "Script/ScriptCompiler.h"
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
#include <windows.h>

namespace Scripting
{
	namespace fs = std::filesystem;

	namespace
	{
		constexpr char kEntryMagic[] = "C4SCRIPT1";
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

		bool AddInputs(Digest& digest, const HostInterface& host, const ScriptCompileOptions& options)
		{
			digest.Add(llvm::StringRef(reinterpret_cast<const char*>(host.buildId.data()), host.buildId.size()));
			digest.Add(CachedName(options.name));
			digest.Add(options.optimize ? "O2" : "O0");
			for (const std::string& entry : options.allow) digest.Add(entry);
			digest.Add("|");
			for (const auto& [name, value] : options.defines) { digest.Add(name); digest.Add(value); }
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

		std::string Encode(const CachedScript& script)
		{
			std::string out(kEntryMagic);
			PutPart(out, script.diagnostics);
			PutPart(out, std::to_string(script.threadLocals.size()));
			for (const std::string& name : script.threadLocals) PutPart(out, name);
			PutPart(out, script.object);
			return out;
		}

		std::optional<CachedScript> Decode(llvm::StringRef in)
		{
			if (!in.consume_front(kEntryMagic)) return std::nullopt;
			CachedScript script;
			std::string count;
			if (!TakePart(in, script.diagnostics) || !TakePart(in, count)) return std::nullopt;
			const uint64 locals = std::strtoull(count.c_str(), nullptr, 10);
			for (uint64 i = 0; i < locals; ++i)
				if (!TakePart(in, script.threadLocals.emplace_back())) return std::nullopt;
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

	std::string ScriptCache::EntryFor(const HostInterface& host, const ScriptCompileOptions& options) const
	{
		Digest digest;
		digest.Add(kEntryMagic);
		digest.Add(m_Compiler);
		if (!AddInputs(digest, host, options)) return "";
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
	}
} // namespace Scripting
