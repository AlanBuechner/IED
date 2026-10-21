#include "Script/ScriptDll.h"
#include "Script/ScriptDllImports.h"
#include <lld/Common/Driver.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

LLD_HAS_DRIVER(coff)

namespace Scripting
{
	namespace
	{
		namespace fs = std::filesystem;
		using ImageFn = void (*)(const void*, const void*);
		using ImageRemoveFn = void (*)(const void*);

		constexpr const char* kImageBase = "__ImageBase"; // the linker's, in every image

		bool Fail(std::string* error, const std::string& why)
		{
			if (error) *error = why;
			return false;
		}

		std::string Narrow(const wchar_t* text)
		{
			const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			std::string narrow(bytes > 0 ? static_cast<size_t>(bytes - 1) : 0, '\0');
			if (bytes > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, narrow.data(), bytes, nullptr, nullptr);
			return narrow;
		}

		std::string Hex(uint64 value)
		{
			char text[24];
			std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
			return text;
		}

		// `address` in the module at `path` (mapped at `image`) as `module!function+0x..`, read from its PDB on a DbgHelp
		// session of its own (a handle no process has), so a host that uses DbgHelp itself is not disturbed. Empty when
		// no symbol covers it.
		std::string SymbolAt(const fs::path& path, const ScriptImage& image, uint64 address)
		{
			static std::mutex dbghelp; // DbgHelp is single-threaded
			std::lock_guard<std::mutex> one(dbghelp);
			HANDLE session = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0xC45C41D0));
			if (!SymInitializeW(session, nullptr, FALSE)) return "";
			std::string text;
			const DWORD size = static_cast<DWORD>(image.high - image.low);
			if (SymLoadModuleExW(session, nullptr, path.wstring().c_str(), nullptr, image.low, size, nullptr, 0))
			{
				constexpr ULONG kLongestName = 512;
				alignas(SYMBOL_INFOW) char buffer[sizeof(SYMBOL_INFOW) + kLongestName * sizeof(wchar_t)] = {};
				auto* symbol = reinterpret_cast<SYMBOL_INFOW*>(buffer);
				symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
				symbol->MaxNameLen = kLongestName;
				DWORD64 displacement = 0;
				if (SymFromAddrW(session, address, &displacement, symbol))
					text = path.stem().string() + "!" + Narrow(symbol->Name) + "+" + Hex(displacement);
			}
			SymCleanup(session);
			return text;
		}

		// A script DLL loaded and its import slots filled: it frees itself, after taking its range out of the runtime's
		// list of script images, and removes its files (the PDB stays while a debugger holds it).
		class DllScript final : public LinkedScript
		{
		public:
			DllScript(HMODULE module, fs::path path, ScriptImage image, ImageRemoveFn remove)
				: m_Module(module), m_Path(std::move(path)), m_Image(image), m_Remove(remove) {}
			~DllScript() override
			{
				if (m_Remove) m_Remove(reinterpret_cast<const void*>(m_Image.low));
				FreeLibrary(m_Module);
				std::error_code ec;
				fs::remove(m_Path, ec);
				fs::remove(fs::path(m_Path).replace_extension(".pdb"), ec);
			}

			void* Find(const std::string& name) const override { return reinterpret_cast<void*>(GetProcAddress(m_Module, name.c_str())); }
			const ScriptImage& Image() const override { return m_Image; }
			std::string Describe(uint64 address) const override
			{
				return address >= m_Image.low && address < m_Image.high ? SymbolAt(m_Path, m_Image, address) : "";
			}

		private:
			HMODULE m_Module;
			fs::path m_Path;
			ScriptImage m_Image;
			ImageRemoveFn m_Remove;
		};

		// Every name the script takes from outside is reached through an import pointer (`__imp_<name>`), declared or kept
		// only for inlining: its code calls and addresses the host's functions and data through them, so the address of
		// a host function in script code is the host's.
		void ImportEveryDeclaration(llvm::Module& module)
		{
			for (llvm::GlobalValue& value : module.global_values())
			{
				if (value.getName().starts_with("llvm.") || value.getName() == kImageBase) continue;
				if (!value.isDeclaration() && !value.hasAvailableExternallyLinkage()) continue;
				value.setDSOLocal(false); // dso_local would keep the direct reference
				value.setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
			}
		}

		// Emulated thread storage replaces a thread_local with its control (`__emutls_v.<name>`), so the variable's debug
		// record could name only a symbol the object no longer has: thread_locals carry none.
		void DropThreadLocalDebugRecords(llvm::Module& module)
		{
			for (llvm::GlobalVariable& variable : module.globals())
				if (variable.isThreadLocal()) variable.eraseMetadata(llvm::LLVMContext::MD_dbg);
		}

		std::optional<std::string> EmitObject(llvm::Module& module, llvm::TargetMachine& machine, std::string* error)
		{
			module.setDataLayout(machine.createDataLayout());
			module.setTargetTriple(machine.getTargetTriple());
			llvm::SmallVector<char, 0> buffer;
			llvm::raw_svector_ostream stream(buffer);
			llvm::legacy::PassManager passes;
			if (machine.addPassesToEmitFile(passes, stream, nullptr, llvm::CodeGenFileType::ObjectFile))
			{
				Fail(error, "the target cannot write an object");
				return std::nullopt;
			}
			passes.run(module);
			return std::string(buffer.data(), buffer.size());
		}

		bool WriteFile(const fs::path& path, const std::string& bytes, std::string* error)
		{
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			if (out) return true;
			return Fail(error, "cannot write " + path.string());
		}

		// lld keeps its state per link; one link at a time. A link that crashed leaves it unusable for the process.
		bool LinkWithLld(const std::vector<std::string>& arguments, std::string* error)
		{
			static std::mutex linking;
			static bool broken = false;
			std::lock_guard<std::mutex> one(linking);
			if (broken) return Fail(error, "the linker crashed in an earlier link: restart the program to link debug scripts");
			std::vector<const char*> argv;
			for (const std::string& argument : arguments) argv.push_back(argument.c_str());
			std::string messages;
			llvm::raw_string_ostream out(messages);
			const lld::Result result = lld::lldMain(argv, out, out, { { lld::WinLink, &lld::coff::link } });
			broken = !result.canRunAgain;
			if (result.retCode == 0) return true;
			return Fail(error, "the script's module does not link: " + messages);
		}

		std::vector<std::string> LinkArguments(const fs::path& dll, const fs::path& pdb, const std::vector<fs::path>& objects, const std::vector<std::string>& exports)
		{
			std::vector<std::string> arguments = { "lld-link", "/dll", "/noentry", "/nodefaultlib", "/machine:x64", "/incremental:no", "/opt:noref",
				"/manifest:no", "/noimplib", "/debug:full", "/pdb:" + pdb.string(), "/out:" + dll.string() };
			for (const fs::path& object : objects) arguments.push_back(object.string());
			for (const std::string& name : exports) arguments.push_back("/export:" + name);
			return arguments;
		}

		// The DLL's range: its base and the size its headers give.
		ScriptImage ImageOf(HMODULE module)
		{
			const auto base = reinterpret_cast<uintptr_t>(module);
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			return { base, base + nt->OptionalHeader.SizeOfImage };
		}
	}

	std::string ScriptDllName(const std::string& tag, uint64 load)
	{
		std::string name = tag.empty() ? "script" : tag;
		for (char& c : name)
			if (c < ' ' || std::strchr("<>:\"/\\|?*", c)) c = '-';
		return name + "-" + std::to_string(load);
	}

	namespace
	{
		bool ProcessRuns(DWORD id)
		{
			HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
			if (!process) return GetLastError() == ERROR_ACCESS_DENIED; // someone else's, so not ours to clean
			DWORD code = 0;
			const bool runs = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
			CloseHandle(process);
			return runs;
		}

		void RemoveEndedProcessFolders(const fs::path& root)
		{
			std::error_code ec;
			for (const fs::directory_entry& entry : fs::directory_iterator(root, ec))
			{
				const std::string name = entry.path().filename().string();
				if (!entry.is_directory(ec) || name.empty() || name.find_first_not_of("0123456789") != std::string::npos) continue;
				if (!ProcessRuns(static_cast<DWORD>(std::stoul(name)))) fs::remove_all(entry.path(), ec);
			}
		}
	}

	std::string ScriptDllFolder(const std::string& root)
	{
		RemoveEndedProcessFolders(root);
		return (fs::path(root) / std::to_string(GetCurrentProcessId())).string();
	}

	void RemoveScriptDllFolder(const std::string& folder)
	{
		std::error_code ec;
		fs::remove(folder, ec); // only when empty: a PDB a debugger holds keeps it for the next host to clean
	}

	std::unique_ptr<LinkedScript> LinkScriptDll(ScriptCode code, const HostSymbolTable& host, const CpuTarget& target,
		const ScriptDllFile& file, std::string* error)
	{
		if (!code.module) { Fail(error, "a debug script links from its module, not from a cached object"); return nullptr; }
		std::unique_ptr<llvm::TargetMachine> machine = ScriptDllTargetMachine(target, code.optimize, error);
		if (!machine) return nullptr;
		ImportEveryDeclaration(*code.module);
		DropThreadLocalDebugRecords(*code.module);
		std::optional<std::string> object = EmitObject(*code.module, *machine, error);
		if (!object) return nullptr;
		std::optional<std::vector<std::string>> definitions = ScriptDefinitions(*object, error);
		if (!definitions) return nullptr;
		std::optional<std::vector<ScriptImport>> imports = ScriptImports(*object, host, error);
		if (!imports) return nullptr;
		std::optional<std::string> importObject = EmitObject(*ScriptImportModule(*code.context, *imports, *machine), *machine, error);
		if (!importObject) return nullptr;

		std::error_code ec;
		fs::create_directories(file.folder, ec);
		const fs::path base = fs::path(file.folder) / file.name;
		const fs::path scriptObject = base.string() + ".obj";
		const fs::path importsObject = base.string() + ".imports.obj";
		const fs::path dll = base.string() + ".dll";
		const fs::path pdb = base.string() + ".pdb";
		if (!WriteFile(scriptObject, *object, error) || !WriteFile(importsObject, *importObject, error)) return nullptr;
		const bool linked = LinkWithLld(LinkArguments(dll, pdb, { scriptObject, importsObject }, *definitions), error);
		fs::remove(scriptObject, ec);
		fs::remove(importsObject, ec);
		if (!linked) return nullptr;

		HMODULE module = LoadLibraryExW(dll.wstring().c_str(), nullptr, 0);
		if (!module) { Fail(error, "the script's module does not load: error " + std::to_string(GetLastError())); return nullptr; }
		if (!BindScriptImports(module, *imports, error)) { FreeLibrary(module); return nullptr; }
		const ScriptImage image = ImageOf(module);
		if (const ImageFn add = host.Function<ImageFn>("c4_script_image_add")) add(reinterpret_cast<const void*>(image.low), reinterpret_cast<const void*>(image.high));
		return std::make_unique<DllScript>(module, dll, image, host.Function<ImageRemoveFn>("c4_script_image_remove"));
	}
} // namespace Scripting
