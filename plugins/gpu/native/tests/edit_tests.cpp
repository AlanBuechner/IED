// Edit and Replay's pieces without a GPU (docs/22 "Edit and Replay"): the SPIR-V listing read back by the assembler -
// the Vulkan sample's shaders to the same words, a constant edited, a new name - compiler logs read into diagnostics,
// shader keys, and HLSL compiled through Windows' compilers.
#include "testing.h"

#include "hlsl.h"
#include "replay/vulkan/vkspirv.h"
#include "shaderedit.h"
#include "shaders.h"

using namespace fb;
using namespace fb::replay::vulkan;

namespace
{
	struct Module
	{
		const char* name;
		const u32* words;
		size_t count;
	};

	const Module kModules[] = {
		{"pattern cs", kPatternCs, sizeof(kPatternCs) / 4}, {"scene vs", kSceneVs, sizeof(kSceneVs) / 4}, {"scene fs", kSceneFs, sizeof(kSceneFs) / 4},
		{"tonemap vs", kTonemapVs, sizeof(kTonemapVs) / 4}, {"tonemap fs", kTonemapFs, sizeof(kTonemapFs) / 4},
	};

	std::vector<u32> Words(const Module& m) { return std::vector<u32>(m.words, m.words + m.count); }
}

TEST(spirv_listing_reads_back_to_the_same_words) {
	for (const Module& m : kModules) {
		const std::string listing = DisassembleSpirv(m.words, m.count);
		std::vector<u32> words;
		std::string log;
		CHECK(AssembleSpirv(listing, m.words, m.count, words, log));
		CHECK(words == Words(m));
		if (words != Words(m)) printf("  %s: %s\n", m.name, log.c_str());
		// Without the original, the names get new ids: the module reads back to itself all the same.
		std::vector<u32> fresh, again;
		CHECK(AssembleSpirv(listing, nullptr, 0, fresh, log));
		CHECK(AssembleSpirv(DisassembleSpirv(fresh.data(), fresh.size()), fresh.data(), fresh.size(), again, log) && again == fresh);
	}
}

TEST(spirv_edits_read_back) {
	const Module& fs = kModules[2];
	std::string listing = DisassembleSpirv(fs.words, fs.count);
	// A float constant edited by its value: one word changes, to the new value's bits.
	const size_t at = listing.find("OpConstant %float ");
	CHECK(at != std::string::npos);
	const size_t value = at + strlen("OpConstant %float "), end = listing.find('\n', value);
	std::string edited = listing.substr(0, value) + "0.25" + listing.substr(end);
	std::vector<u32> words;
	std::string log;
	CHECK(AssembleSpirv(edited, fs.words, fs.count, words, log));
	u32 changed = 0, changedTo = 0;
	for (size_t i = 0; i < words.size() && i < fs.count; ++i)
		if (words[i] != fs.words[i]) ++changed, changedTo = words[i];
	CHECK(words.size() == fs.count && changed == 1 && changedTo == 0x3e800000);
	// A new name gets the next id: the bound grows by one.
	const size_t line = listing.find('\n', at);
	edited = listing.substr(0, line + 1) + "%mine = OpConstant %float 2\n" + listing.substr(line + 1);
	CHECK(AssembleSpirv(edited, fs.words, fs.count, words, log));
	CHECK(words.size() == fs.count + 4 && words[3] == fs.words[3] + 1);
	// What does not read says where.
	CHECK(!AssembleSpirv("; a comment\n%x = OpFoo %float\nOpCapability Shader\n%y = OpFAdd %float %x\n", nullptr, 0, words, log));
	const std::vector<Diagnostic> problems = ParseCompilerLog(log);
	CHECK(problems.size() == 1 && problems[0].line == 2 && problems[0].text == "no instruction OpFoo");
	CHECK(!AssembleSpirv("OpName %x \"unclosed\n", nullptr, 0, words, log) && log.find("line 1:") == 0);
}

TEST(compiler_logs_read_into_diagnostics) {
	const std::vector<Diagnostic> fxc = ParseCompilerLog(
		"C:\\work\\edited.hlsl(12,5-9): error X3004: undeclared identifier 'colr'\n"
		"edited.hlsl(3,1): warning X3206: implicit truncation of vector type\n");
	CHECK(fxc.size() == 2);
	CHECK(fxc.size() == 2 && fxc[0].line == 12 && fxc[0].column == 5 && fxc[0].severity == "error" && fxc[0].text == "undeclared identifier 'colr'");
	CHECK(fxc.size() == 2 && fxc[1].line == 3 && fxc[1].severity == "warning");
	const std::vector<Diagnostic> dxc = ParseCompilerLog(
		"edited.hlsl:7:14: error: use of undeclared identifier 'colr'\n"
		"    return colr;\n"
		"             ^\n"
		"C:\\shaders\\a.glsl:2: error: 'x' : undeclared identifier\n");
	CHECK(dxc.size() == 2);
	CHECK(dxc.size() == 2 && dxc[0].line == 7 && dxc[0].column == 14 && dxc[0].text == "use of undeclared identifier 'colr'");
	CHECK(dxc.size() == 2 && dxc[1].line == 2 && dxc[1].column == 0);
	CHECK(ShaderKey("abc", 3) == ShaderKey("abc", 3) && ShaderKey("abc", 3) != ShaderKey("abd", 3) && ShaderKey("", 0) == "cbf29ce484222325");
}

TEST(hlsl_compiles_through_windows_compilers) {
	const std::string source = "float4 PSMain(float4 p : SV_Position) : SV_Target { return p * 0.5; }\n";
	std::vector<u8> code;
	std::string log;
	CHECK(CompileHlsl(source, "PSMain", "ps_5_1", HlslTarget::Dxbc, code, log));
	CHECK(code.size() > 32 && memcmp(code.data(), "DXBC", 4) == 0);
	CHECK(!CompileHlsl("float4 PSMain() : SV_Target { return colr; }", "PSMain", "ps_5_1", HlslTarget::Dxbc, code, log));
	const std::vector<Diagnostic> problems = ParseCompilerLog(log);
	CHECK(!problems.empty() && problems[0].line == 1 && problems[0].severity == "error");
	if (DxCompiler()) {
		CHECK(CompileHlsl(source, "PSMain", "ps_6_0", HlslTarget::Dxil, code, log));
		CHECK(code.size() > 32 && memcmp(code.data(), "DXBC", 4) == 0);
	}
	if (SpirvDxCompiler()) {
		CHECK(CompileHlsl(source, "PSMain", "ps_6_0", HlslTarget::Spirv, code, log));
		CHECK(code.size() > 20 && *(const u32*)code.data() == 0x07230203);
	}
}
