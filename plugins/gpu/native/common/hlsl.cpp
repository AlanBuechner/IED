// hlsl.cpp - HLSL through Windows' and the SDKs' compilers (hlsl.h).
#include "hlsl.h"

#include <windows.h>
#include <d3dcompiler.h>
#include <dxcapi.h>

namespace fb
{
	namespace
	{
		// A DLL by its full path, its own dependencies (dxil.dll beside dxcompiler.dll) found beside it.
		HMODULE LoadBeside(const std::wstring& path) {
			return path.empty() ? nullptr : LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		}

		std::wstring Environment(const wchar_t* name) {
			wchar_t value[MAX_PATH * 2] = {};
			return GetEnvironmentVariableW(name, value, MAX_PATH * 2) ? std::wstring(value) : std::wstring();
		}

		// The newest `pattern` folder under `root` holding `file`; "" when none does.
		std::wstring Newest(const std::wstring& root, const wchar_t* pattern, const wchar_t* file) {
			WIN32_FIND_DATAW found;
			std::wstring newest;
			HANDLE search = FindFirstFileW((root + pattern).c_str(), &found);
			if (search == INVALID_HANDLE_VALUE) return newest;
			do {
				const std::wstring candidate = root + found.cFileName + file;
				if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES && candidate > newest) newest = candidate;
			} while (FindNextFileW(search, &found));
			FindClose(search);
			return newest;
		}

		bool CompileDxbc(const std::string& source, const std::string& entry, const std::string& profile, std::vector<u8>& code, std::string& log) {
			auto compile = Compiler47() ? (pD3DCompile)GetProcAddress((HMODULE)Compiler47(), "D3DCompile") : nullptr;
			if (!compile) {
				log = "d3dcompiler_47.dll is not there";
				return false;
			}
			ID3DBlob* object = nullptr;
			ID3DBlob* errors = nullptr;
			const HRESULT hr = compile(source.data(), source.size(), "edited.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry.c_str(), profile.c_str(), 0, 0, &object, &errors);
			if (errors) log.assign((const char*)errors->GetBufferPointer(), errors->GetBufferSize());
			if (object) code.assign((const u8*)object->GetBufferPointer(), (const u8*)object->GetBufferPointer() + object->GetBufferSize());
			if (object) object->Release();
			if (errors) errors->Release();
			while (!log.empty() && log.back() == '\0') log.pop_back();
			return SUCCEEDED(hr) && !code.empty();
		}

		bool CompileDxc(HMODULE module, bool spirv, const std::string& source, const std::string& entry, const std::string& profile, std::vector<u8>& code, std::string& log) {
			auto create = module ? (DxcCreateInstanceProc)GetProcAddress(module, "DxcCreateInstance") : nullptr;
			IDxcCompiler3* compiler = nullptr;
			if (!create || FAILED(create(CLSID_DxcCompiler, __uuidof(IDxcCompiler3), (void**)&compiler))) {
				log = spirv ? "no dxcompiler.dll with SPIR-V code generation is there (the Vulkan SDK has one; FLASHBANG_SPIRV_DXCOMPILER names it)"
					: "dxcompiler.dll is not there (the Windows SDK has it; FLASHBANG_DXCOMPILER names one)";
				return false;
			}
			const std::wstring wideEntry = Widen(entry), wideProfile = Widen(profile);
			std::vector<LPCWSTR> args = {L"edited.hlsl", L"-E", wideEntry.c_str(), L"-T", wideProfile.c_str()};
			if (spirv) args.insert(args.end(), {L"-spirv", L"-fspv-target-env=vulkan1.1"});
			const DxcBuffer buffer = {source.data(), source.size(), DXC_CP_UTF8};
			IDxcResult* result = nullptr;
			bool built = false;
			if (SUCCEEDED(compiler->Compile(&buffer, args.data(), (UINT32)args.size(), nullptr, __uuidof(IDxcResult), (void**)&result)) && result) {
				IDxcBlobUtf8* errors = nullptr;
				if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, __uuidof(IDxcBlobUtf8), (void**)&errors, nullptr)) && errors) {
					log.assign(errors->GetStringPointer(), errors->GetStringLength());
					errors->Release();
				}
				HRESULT status = E_FAIL;
				IDxcBlob* object = nullptr;
				if (SUCCEEDED(result->GetStatus(&status)) && SUCCEEDED(status) &&
					SUCCEEDED(result->GetOutput(DXC_OUT_OBJECT, __uuidof(IDxcBlob), (void**)&object, nullptr)) && object) {
					code.assign((const u8*)object->GetBufferPointer(), (const u8*)object->GetBufferPointer() + object->GetBufferSize());
					object->Release();
					built = !code.empty();
				}
				result->Release();
			}
			compiler->Release();
			return built;
		}
	}

	void* Compiler47() {
		static HMODULE module = LoadLibraryW(L"d3dcompiler_47.dll");
		return module;
	}

	void* DxCompiler() {
		static HMODULE module = [] {
			const std::wstring named = Environment(L"FLASHBANG_DXCOMPILER");
			if (!named.empty()) return LoadBeside(named);
			HMODULE found = LoadBeside(Newest(L"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\", L"10.*", L"\\x64\\dxcompiler.dll"));
			return found ? found : LoadLibraryW(L"dxcompiler.dll");
		}();
		return module;
	}

	void* SpirvDxCompiler() {
		static HMODULE module = [] {
			std::wstring path = Environment(L"FLASHBANG_SPIRV_DXCOMPILER");
			if (path.empty() && !Environment(L"VULKAN_SDK").empty()) path = Environment(L"VULKAN_SDK") + L"\\Bin\\dxcompiler.dll";
			if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) path = Newest(L"C:\\VulkanSDK\\", L"*", L"\\Bin\\dxcompiler.dll");
			return LoadBeside(path);
		}();
		return module;
	}

	bool CompileHlsl(const std::string& source, const std::string& entry, const std::string& profile, HlslTarget target, std::vector<u8>& code, std::string& log) {
		code.clear();
		log.clear();
		switch (target) {
			case HlslTarget::Dxbc: return CompileDxbc(source, entry, profile, code, log);
			case HlslTarget::Dxil: return CompileDxc((HMODULE)DxCompiler(), false, source, entry, profile, code, log);
			default: return CompileDxc((HMODULE)SpirvDxCompiler(), true, source, entry, profile, code, log);
		}
	}
}
