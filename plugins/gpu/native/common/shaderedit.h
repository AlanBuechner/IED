// shaderedit.h - what Edit and Replay (docs/22 "Edit and Replay") shares between the APIs: a shader's key, the same
// for the same compiled shader in any capture of a program, and a compiler's log read into diagnostics.
#pragma once

#include "fb_base.h"
#include "json.h"

#include <string>
#include <vector>

namespace fb
{
	// FNV-1a 64 of a shader's bytes as 16 hex digits.
	std::string ShaderKey(const void* bytes, size_t size);

	struct Diagnostic
	{
		u32 line = 0;            // 1-based; 0 when the message names no line
		u32 column = 0;
		std::string severity;    // "error", "warning"
		std::string text;
	};

	// The diagnostics of a compiler's log: fxc's "name(line,col-col): error X3000: text", dxc's and glslang's
	// "name:line:col: error: text", and Flashbang's assembler's "line N: text"; lines it cannot read are left out.
	std::vector<Diagnostic> ParseCompilerLog(const std::string& log);
	// `log` and its `diagnostics` ([{line, column, severity, text}]) into an answer.
	void WriteDiagnostics(JsonWriter& out, const std::string& log);
}
