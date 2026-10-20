// The script host's C API over Script/ScriptHost (docs/runtime/04).

#define IMPLEMENT_LOGGER // the DLL's own copy of Frost's loggers, which C4Lib logs through
#include "Core/Logging/Log.h"
#include "C4Script.h"
#include "Script/ScriptHost.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <windows.h>

struct c4s_host
{
	std::unique_ptr<Scripting::ScriptHost> host;
};

struct c4s_script
{
	std::unique_ptr<Scripting::Script> script;
	std::string tag;
	std::string fault; // FaultText, kept for the pointer handed out
};

namespace
{
	// The guard ScriptHost runs a script's starts, exits and calls in.
	bool Guard(void (*fn)(void*), void* context, Scripting::ScriptFault* fault)
	{
		c4s_fault seen = {};
		if (c4s_guarded_call(fn, context, &seen)) return true;
		if (fault) *fault = { seen.code, seen.write, seen.address, seen.target };
		return false;
	}

	std::string ExecutableInterface()
	{
		wchar_t buffer[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (length == 0 || length >= MAX_PATH) return {};
		return std::filesystem::path(buffer).replace_extension(".c4i").string();
	}

	void CopyError(const std::string& message, char* error, uint64_t errorSize)
	{
		if (!error || errorSize == 0) return;
		const size_t length = std::min<size_t>(message.size(), static_cast<size_t>(errorSize - 1));
		std::memcpy(error, message.data(), length);
		error[length] = 0;
	}

	Scripting::ScriptCompileOptions OptionsOf(const c4s_options& from)
	{
		Scripting::ScriptCompileOptions options;
		options.name = from.name ? from.name : "script";
		for (uint32_t i = 0; i < from.fileCount; ++i)
		{
			Scripting::ScriptFile file;
			file.path = from.paths && from.paths[i] ? from.paths[i] : "";
			if (from.texts && from.texts[i]) file.text = from.texts[i];
			options.files.push_back(std::move(file));
		}
		for (uint32_t i = 0; i < from.allowCount; ++i) options.allow.push_back(from.allow[i]);
		for (uint32_t i = 0; i < from.defineCount; ++i)
		{
			const std::string define = from.defines[i];
			const size_t equals = define.find('=');
			options.defines[define.substr(0, equals)] = equals == std::string::npos ? "1" : define.substr(equals + 1);
		}
		options.optimize = from.optimize != 0;
		return options;
	}
}

extern "C"
{
	c4s_host* c4s_host_open(const char* interfacePath, const void* table, char* error, uint64_t errorSize)
	{
		const std::string path = interfacePath && *interfacePath ? interfacePath : ExecutableInterface();
		std::string why;
		std::unique_ptr<Scripting::ScriptHost> host = Scripting::ScriptHost::Open(path, table, Guard, &why);
		if (!host)
		{
			CopyError(why, error, errorSize);
			return nullptr;
		}
		return new c4s_host{ std::move(host) };
	}

	void c4s_host_close(c4s_host* host) { delete host; }

	c4s_script* c4s_compile(c4s_host* host, const c4s_options* options)
	{
		auto* result = new c4s_script();
		result->tag = options && options->name ? options->name : "script";
		if (host && options) result->script = host->host->Compile(OptionsOf(*options));
		return result;
	}

	c4s_script* c4s_reload(c4s_host* host, c4s_script* old, const c4s_options* options)
	{
		if (!old || !old->script) return c4s_compile(host, options);
		auto* result = new c4s_script();
		result->tag = options && options->name ? options->name : "script";
		if (host && options) result->script = host->host->Reload(*old->script, OptionsOf(*options));
		return result;
	}

	int32_t c4s_script_ok(const c4s_script* script) { return script && script->script && script->script->Ok() ? 1 : 0; }

	const char* c4s_script_diagnostics(const c4s_script* script)
	{
		if (!script) return "";
		return script->script ? script->script->Diagnostics().c_str() : "error: no script host to compile with\n";
	}

	const char* c4s_script_tag(const c4s_script* script) { return script ? script->tag.c_str() : ""; }

	int32_t c4s_script_faulted(const c4s_script* script) { return script && script->script && script->script->Faulted() ? 1 : 0; }

	const char* c4s_script_fault(c4s_script* script)
	{
		if (!script || !script->script) return "";
		script->fault = script->script->FaultText();
		return script->fault.c_str();
	}

	void* c4s_script_find(const c4s_script* script, const char* symbol)
	{
		return script && script->script && symbol ? script->script->Find(symbol) : nullptr;
	}

	uint64_t c4s_script_unload(c4s_script* script) { return script && script->script ? script->script->Unload() : 0; }

	void c4s_script_destroy(c4s_script* script) { delete script; }
}

int32_t c4s_script_call(c4s_script* script, void (*fn)(void*), void* context)
{
	return script && script->script && script->script->Call(fn, context) ? 1 : 0;
}
