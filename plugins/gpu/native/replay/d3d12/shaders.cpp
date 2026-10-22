// A pipeline's shader for the UI (docs/22 "Views"): its kind (DXBC or DXIL), disassembly and reflection - bindings,
// constant buffers with their variables, inputs and outputs. DXBC goes through Windows' own d3dcompiler_47.dll; DXIL
// through dxcompiler.dll from the Windows SDK (FLASHBANG_DXCOMPILER names another), when it is there.
#include "bufferlayout.h"
#include "d3d12replay.h"

#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <windows.h>

namespace fb::replay::d3d12
{
	bool IsDxil(const D3D12_SHADER_BYTECODE& code) {
		const u8* bytes = (const u8*)code.pShaderBytecode;
		if (code.BytecodeLength < 32 || memcmp(bytes, "DXBC", 4) != 0) return false;
		u32 parts = 0;
		memcpy(&parts, bytes + 28, 4);
		for (u32 i = 0; i < parts && 32 + (size_t)i * 4 + 4 <= code.BytecodeLength; ++i) {
			u32 offset = 0;
			memcpy(&offset, bytes + 32 + i * 4, 4);
			if ((size_t)offset + 4 <= code.BytecodeLength && memcmp(bytes + offset, "DXIL", 4) == 0) return true;
		}
		return false;
	}

	namespace
	{
		using DisassembleFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob**);
		using ReflectFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, REFIID, void**);

		HMODULE Compiler47() {
			static HMODULE module = LoadLibraryW(L"d3dcompiler_47.dll");
			return module;
		}

		HMODULE DxCompiler() {
			static HMODULE module = [] {
				wchar_t named[MAX_PATH * 2] = {};
				if (GetEnvironmentVariableW(L"FLASHBANG_DXCOMPILER", named, MAX_PATH * 2)) return LoadLibraryW(named);
				WIN32_FIND_DATAW found;
				std::wstring newest;
				const std::wstring root = L"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\";
				HANDLE search = FindFirstFileW((root + L"10.*").c_str(), &found);
				if (search != INVALID_HANDLE_VALUE) {
					do {
						const std::wstring candidate = root + found.cFileName + L"\\x64\\dxcompiler.dll";
						if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES && candidate > newest) newest = candidate;
					} while (FindNextFileW(search, &found));
					FindClose(search);
				}
				HMODULE module = newest.empty() ? nullptr : LoadLibraryW(newest.c_str());
				return module ? module : LoadLibraryW(L"dxcompiler.dll");
			}();
			return module;
		}

		std::string TypeText(ID3D12ShaderReflectionType* type) {
			D3D12_SHADER_TYPE_DESC desc;
			if (!type || FAILED(type->GetDesc(&desc))) return "?";
			std::string base;
			switch (desc.Type) {
				case D3D_SVT_FLOAT: base = "float"; break;
				case D3D_SVT_INT: base = "int"; break;
				case D3D_SVT_UINT: base = "uint"; break;
				case D3D_SVT_BOOL: base = "bool"; break;
				case D3D_SVT_DOUBLE: base = "double"; break;
				case D3D_SVT_MIN16FLOAT: base = "min16float"; break;
				case D3D_SVT_FLOAT16: base = "half"; break;
				default: base = desc.Name ? desc.Name : "struct"; break;
			}
			std::string text = base;
			if (desc.Class == D3D_SVC_VECTOR) text += std::to_string(desc.Columns);
			else if (desc.Class == D3D_SVC_MATRIX_ROWS || desc.Class == D3D_SVC_MATRIX_COLUMNS) text += Format("%ux%u", desc.Rows, desc.Columns);
			if (desc.Elements) text += Format("[%u]", desc.Elements);
			return text;
		}

		const char* InputTypeName(D3D_SHADER_INPUT_TYPE type) {
			switch (type) {
				case D3D_SIT_CBUFFER: return "cbuffer";
				case D3D_SIT_TBUFFER: return "tbuffer";
				case D3D_SIT_TEXTURE: return "texture";
				case D3D_SIT_SAMPLER: return "sampler";
				case D3D_SIT_UAV_RWTYPED: return "rw texture";
				case D3D_SIT_STRUCTURED: return "structured buffer";
				case D3D_SIT_UAV_RWSTRUCTURED: return "rw structured buffer";
				case D3D_SIT_BYTEADDRESS: return "byte address buffer";
				case D3D_SIT_UAV_RWBYTEADDRESS: return "rw byte address buffer";
				case D3D_SIT_RTACCELERATIONSTRUCTURE: return "acceleration structure";
				default: return "resource";
			}
		}

		const char* Letter(D3D_SHADER_INPUT_TYPE type) {
			switch (type) {
				case D3D_SIT_CBUFFER: return "b";
				case D3D_SIT_SAMPLER: return "s";
				case D3D_SIT_UAV_RWTYPED:
				case D3D_SIT_UAV_RWSTRUCTURED:
				case D3D_SIT_UAV_RWBYTEADDRESS:
				case D3D_SIT_UAV_APPEND_STRUCTURED:
				case D3D_SIT_UAV_CONSUME_STRUCTURED:
				case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
				case D3D_SIT_UAV_FEEDBACKTEXTURE: return "u";
				default: return "t";
			}
		}

		// The shader's reflection (DXBC through d3dcompiler_47, DXIL through dxcompiler), or null when the compiler it
		// needs is not here. Release it.
		ID3D12ShaderReflection* Reflect(const D3D12_SHADER_BYTECODE& code) {
			ID3D12ShaderReflection* reflection = nullptr;
			if (!IsDxil(code)) {
				HMODULE module = Compiler47();
				auto reflect = module ? (ReflectFn)GetProcAddress(module, "D3DReflect") : nullptr;
				if (reflect) reflect(code.pShaderBytecode, code.BytecodeLength, __uuidof(ID3D12ShaderReflection), (void**)&reflection);
				return reflection;
			}
			HMODULE module = DxCompiler();
			auto create = module ? (DxcCreateInstanceProc)GetProcAddress(module, "DxcCreateInstance") : nullptr;
			IDxcUtils* utils = nullptr;
			if (create && SUCCEEDED(create(CLSID_DxcUtils, __uuidof(IDxcUtils), (void**)&utils))) {
				const DxcBuffer buffer = {code.pShaderBytecode, code.BytecodeLength, DXC_CP_ACP};
				utils->CreateReflection(&buffer, __uuidof(ID3D12ShaderReflection), (void**)&reflection);
				utils->Release();
			}
			return reflection;
		}

		// A cbuffer variable placed by HLSL's packing: matrix columns (rows when row-major) and array elements each start
		// a 16-byte register; a struct has no vector type.
		BlockMember Placed(ID3D12ShaderReflectionVariable* variable) {
			BlockMember out;
			D3D12_SHADER_VARIABLE_DESC v;
			D3D12_SHADER_TYPE_DESC t;
			if (!variable || FAILED(variable->GetDesc(&v)) || !variable->GetType() || FAILED(variable->GetType()->GetDesc(&t))) return out;
			out.name = v.Name ? v.Name : "";
			out.offset = v.StartOffset;
			out.bytes = v.Size;
			const char* base = nullptr;
			u32 bytes = 4;
			switch (t.Type) {
				case D3D_SVT_FLOAT: case D3D_SVT_MIN16FLOAT: case D3D_SVT_MIN10FLOAT: base = "float"; break;
				case D3D_SVT_INT: case D3D_SVT_MIN16INT: case D3D_SVT_MIN12INT: base = "int"; break;
				case D3D_SVT_UINT: case D3D_SVT_MIN16UINT: base = "uint"; break;
				case D3D_SVT_BOOL: base = "bool"; break;
				case D3D_SVT_DOUBLE: base = "double", bytes = 8; break;
				case D3D_SVT_FLOAT16: base = "half", bytes = 2; break;
				case D3D_SVT_INT16: base = "int16", bytes = 2; break;
				case D3D_SVT_UINT16: base = "uint16", bytes = 2; break;
				case D3D_SVT_INT64: base = "int64", bytes = 8; break;
				case D3D_SVT_UINT64: base = "uint64", bytes = 8; break;
				default: return out;
			}
			u32 components = 1;
			switch (t.Class) {
				case D3D_SVC_SCALAR: break;
				case D3D_SVC_VECTOR: components = t.Columns; break;
				case D3D_SVC_MATRIX_COLUMNS: out.vectors = t.Columns, components = t.Rows, out.vectorStride = 16; break;
				case D3D_SVC_MATRIX_ROWS: out.vectors = t.Rows, components = t.Columns, out.vectorStride = 16; break;
				default: return out;
			}
			out.vectorType = VectorType(base, components);
			out.vectorBytes = bytes * components;
			if (out.vectors == 1) out.vectorStride = out.vectorBytes;
			out.elements = t.Elements ? t.Elements : 1;
			out.elementStride = ((out.vectors - 1) * out.vectorStride + out.vectorBytes + 15) / 16 * 16;
			return out;
		}

		void Reflection(ID3D12ShaderReflection* reflection, JsonWriter& out) {
			D3D12_SHADER_DESC desc;
			if (FAILED(reflection->GetDesc(&desc))) return;
			const u32 kind = D3D12_SHVER_GET_TYPE(desc.Version);
			static const char* prefixes[] = {"ps", "vs", "gs", "hs", "ds", "cs", "lib", "", "", "", "", "", "", "as", "ms"};
			out.String("model", Format("%s_%u_%u", kind < 15 ? prefixes[kind] : "?", D3D12_SHVER_GET_MAJOR(desc.Version), D3D12_SHVER_GET_MINOR(desc.Version)));
			if (desc.Creator) out.String("creator", desc.Creator);
			out.Uint("instructions", desc.InstructionCount);
			out.BeginArray("bindings");
			for (UINT i = 0; i < desc.BoundResources; ++i) {
				D3D12_SHADER_INPUT_BIND_DESC bind;
				if (FAILED(reflection->GetResourceBindingDesc(i, &bind))) continue;
				out.BeginObject();
				out.String("name", bind.Name ? bind.Name : "");
				out.String("type", InputTypeName(bind.Type));
				out.String("register", Format("%s%u", Letter(bind.Type), bind.BindPoint));
				out.Uint("space", bind.Space);
				out.Uint("count", bind.BindCount);
				out.EndObject();
			}
			out.EndArray();
			out.BeginArray("constantBuffers");
			for (UINT i = 0; i < desc.ConstantBuffers; ++i) {
				ID3D12ShaderReflectionConstantBuffer* buffer = reflection->GetConstantBufferByIndex(i);
				D3D12_SHADER_BUFFER_DESC bufferDesc;
				if (!buffer || FAILED(buffer->GetDesc(&bufferDesc))) continue;
				out.BeginObject();
				out.String("name", bufferDesc.Name ? bufferDesc.Name : "");
				out.Uint("size", bufferDesc.Size);
				out.BeginArray("variables");
				for (UINT v = 0; v < bufferDesc.Variables; ++v) {
					ID3D12ShaderReflectionVariable* variable = buffer->GetVariableByIndex(v);
					D3D12_SHADER_VARIABLE_DESC variableDesc;
					if (!variable || FAILED(variable->GetDesc(&variableDesc))) continue;
					out.BeginObject();
					out.String("name", variableDesc.Name ? variableDesc.Name : "");
					out.Uint("offset", variableDesc.StartOffset);
					out.Uint("size", variableDesc.Size);
					out.String("type", TypeText(variable->GetType()));
					out.EndObject();
				}
				out.EndArray();
				out.EndObject();
			}
			out.EndArray();
			auto parameters = [&](const char* key, UINT count, bool input) {
				out.BeginArray(key);
				for (UINT i = 0; i < count; ++i) {
					D3D12_SIGNATURE_PARAMETER_DESC p;
					if (FAILED(input ? reflection->GetInputParameterDesc(i, &p) : reflection->GetOutputParameterDesc(i, &p))) continue;
					out.BeginObject();
					out.String("semantic", Format("%s%u", p.SemanticName ? p.SemanticName : "?", p.SemanticIndex));
					out.Uint("register", p.Register);
					out.Uint("mask", p.Mask);
					out.EndObject();
				}
				out.EndArray();
			};
			parameters("inputs", desc.InputParameters, true);
			parameters("outputs", desc.OutputParameters, false);
		}
	}

	void D3D12Replay::Shader(const JsonValue& params, JsonWriter& out) {
		const std::string stage = params["stage"].Str("ps");
		u64 pipeline = params["pipeline"].Uint(0);
		if (!pipeline) pipeline = Simulate(model_, (u32)params["eid"].Uint(0)).pipeline;
		const ObjectEntry* entry = model_.Find(pipeline);
		PipelineCreate pso = {};
		if (!entry || !model_.DecodeModel(entry->record, entry->recordSize, pso)) Fail("no pipeline state at that event");
		const D3D12_SHADER_BYTECODE* code = stage == "vs" ? &pso.vs : stage == "hs" ? &pso.hs : stage == "ds" ? &pso.ds : stage == "gs" ? &pso.gs
			: stage == "cs" ? &pso.cs : stage == "as" ? &pso.as : stage == "ms" ? &pso.ms : &pso.ps;
		if (!code->pShaderBytecode || !code->BytecodeLength) Fail("the pipeline has no %s shader", stage.c_str());
		const bool dxil = IsDxil(*code);
		out.String("stage", stage);
		out.String("pipeline", model_.NameOf(pipeline));
		out.String("kind", dxil ? "DXIL" : "DXBC");
		out.Uint("size", code->BytecodeLength);
		std::string disassembly;
		if (!dxil) {
			HMODULE module = Compiler47();
			auto disassemble = module ? (DisassembleFn)GetProcAddress(module, "D3DDisassemble") : nullptr;
			ID3DBlob* text = nullptr;
			if (disassemble && SUCCEEDED(disassemble(code->pShaderBytecode, code->BytecodeLength, 0, nullptr, &text))) {
				disassembly.assign((const char*)text->GetBufferPointer(), text->GetBufferSize());
				text->Release();
			}
		}
		else {
			HMODULE module = DxCompiler();
			auto create = module ? (DxcCreateInstanceProc)GetProcAddress(module, "DxcCreateInstance") : nullptr;
			IDxcUtils* utils = nullptr;
			IDxcCompiler3* compiler = nullptr;
			if (create && SUCCEEDED(create(CLSID_DxcUtils, __uuidof(IDxcUtils), (void**)&utils)) && SUCCEEDED(create(CLSID_DxcCompiler, __uuidof(IDxcCompiler3), (void**)&compiler))) {
				const DxcBuffer buffer = {code->pShaderBytecode, code->BytecodeLength, DXC_CP_ACP};
				IDxcResult* result = nullptr;
				if (SUCCEEDED(compiler->Disassemble(&buffer, __uuidof(IDxcResult), (void**)&result))) {
					IDxcBlobUtf8* text = nullptr;
					if (SUCCEEDED(result->GetOutput(DXC_OUT_DISASSEMBLY, __uuidof(IDxcBlobUtf8), (void**)&text, nullptr)) && text) {
						disassembly.assign(text->GetStringPointer(), text->GetStringLength());
						text->Release();
					}
					result->Release();
				}
			}
			else out.String("problem", "DXIL needs dxcompiler.dll (the Windows SDK's, or FLASHBANG_DXCOMPILER)");
			if (compiler) compiler->Release();
			if (utils) utils->Release();
		}
		out.String("disassembly", disassembly);
		if (ID3D12ShaderReflection* reflection = Reflect(*code)) {
			Reflection(reflection, out);
			reflection->Release();
		}
	}

	std::map<std::pair<u32, u32>, std::string> ConstantBufferLayouts(const PipelineCreate& pso) {
		std::map<std::pair<u32, u32>, std::string> layouts;
		for (const D3D12_SHADER_BYTECODE* code : {&pso.vs, &pso.hs, &pso.ds, &pso.gs, &pso.ps, &pso.cs, &pso.as, &pso.ms}) {
			if (!code->pShaderBytecode || !code->BytecodeLength) continue;
			ID3D12ShaderReflection* reflection = Reflect(*code);
			D3D12_SHADER_DESC desc;
			if (!reflection || FAILED(reflection->GetDesc(&desc))) {
				if (reflection) reflection->Release();
				continue;
			}
			for (UINT i = 0; i < desc.BoundResources; ++i) {
				D3D12_SHADER_INPUT_BIND_DESC bind;
				if (FAILED(reflection->GetResourceBindingDesc(i, &bind)) || bind.Type != D3D_SIT_CBUFFER || !bind.Name) continue;
				ID3D12ShaderReflectionConstantBuffer* buffer = reflection->GetConstantBufferByName(bind.Name);
				D3D12_SHADER_BUFFER_DESC bufferDesc;
				if (!buffer || FAILED(buffer->GetDesc(&bufferDesc))) continue;
				std::vector<BlockMember> members;
				for (UINT v = 0; v < bufferDesc.Variables; ++v) members.push_back(Placed(buffer->GetVariableByIndex(v)));
				const std::string layout = BlockLayout(members, bufferDesc.Size);
				for (UINT r = 0; r < std::max<UINT>(1, bind.BindCount) && r < 64; ++r) layouts.emplace(std::make_pair(bind.Space, bind.BindPoint + r), layout);
			}
			reflection->Release();
		}
		return layouts;
	}

	std::string DxbcListing(const D3D12_SHADER_BYTECODE& code, std::map<u32, u32>& lines) {
		HMODULE module = Compiler47();
		auto disassemble = module ? (DisassembleFn)GetProcAddress(module, "D3DDisassemble") : nullptr;
		ID3DBlob* text = nullptr;
		if (!disassemble || FAILED(disassemble(code.pShaderBytecode, code.BytecodeLength, D3D_DISASM_ENABLE_INSTRUCTION_OFFSET, nullptr, &text))) return "";
		std::string raw((const char*)text->GetBufferPointer(), text->GetBufferSize());
		text->Release();
		// Each instruction's line starts "0x0000001C: "; the listing keeps what follows.
		std::string listing;
		u32 line = 0;
		for (size_t at = 0; at < raw.size();) {
			size_t end = raw.find('\n', at);
			if (end == std::string::npos) end = raw.size();
			std::string one = raw.substr(at, end - at);
			at = end + 1;
			while (!one.empty() && (one.back() == '\r' || one.back() == '\0')) one.pop_back();
			unsigned offset = 0;
			if (one.size() >= 12 && one.compare(0, 2, "0x") == 0 && one[10] == ':' && sscanf(one.c_str(), "0x%x:", &offset) == 1) {
				lines[offset] = line;
				one = one.substr(12);
			}
			listing += one + "\n";
			++line;
		}
		return listing;
	}

	bool ShaderWritesStorage(const D3D12_SHADER_BYTECODE& code, bool& known) {
		known = !code.pShaderBytecode || !code.BytecodeLength;
		if (known) return false;
		ID3D12ShaderReflection* reflection = Reflect(code);
		D3D12_SHADER_DESC desc;
		if (!reflection || FAILED(reflection->GetDesc(&desc))) {
			if (reflection) reflection->Release();
			return false;
		}
		known = true;
		bool writes = false;
		for (UINT i = 0; i < desc.BoundResources && !writes; ++i) {
			D3D12_SHADER_INPUT_BIND_DESC bind;
			if (SUCCEEDED(reflection->GetResourceBindingDesc(i, &bind))) writes = !strcmp(Letter(bind.Type), "u");
		}
		reflection->Release();
		return writes;
	}
}
