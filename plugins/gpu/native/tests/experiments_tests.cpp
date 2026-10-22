// The experiments' arithmetic and shaders (common/experiments.cpp, docs/22 "Performance"): shares of the captured time
// and the limiter from made-up times, a range too short to tell, and the trivial pixel shaders - HLSL that compiles,
// a SPIR-V listing that assembles - writing the targets the captured shader wrote.
#include "testing.h"

#include "experiments.h"
#include "hlsl.h"
#include "replay/vulkan/vkspirv.h"

#include <cmath>

using namespace fb;
using namespace fb::replay::vulkan;

namespace
{
	void Runs(ExperimentRun (&runs)[kExperiments], double captured, double trivial, double onePixel, double noWrites) {
		const double ms[] = {captured, trivial, onePixel, noWrites};
		for (u32 e = 0; e < kExperiments; ++e) {
			runs[e].ms = ms[e];
			runs[e].changed = e ? 3 : 0;
		}
	}

	bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }
}

TEST(experiments_find_the_limiter) {
	ExperimentRun runs[kExperiments];
	Runs(runs, 2.0, 0.6, 0.4, 1.9);
	Limits limits = LimitsOf(runs);
	CHECK(Near(limits.vertices, 0.2) && Near(limits.pixelShading, 0.7) && Near(limits.rasterization, 0.1) && Near(limits.colorOutput, 0.05));
	CHECK(limits.limiter == "pixelShading" && !limits.tooShort);
	// A change that measured slower than captured is noise: a share of 0, never below.
	Runs(runs, 1.0, 1.2, 0.9, 0.2);
	limits = LimitsOf(runs);
	CHECK(Near(limits.pixelShading, 0) && Near(limits.rasterization, 0.3) && Near(limits.colorOutput, 0.8) && limits.limiter == "vertices");
	// An experiment that changed no draw says nothing.
	Runs(runs, 1.0, 0.5, 0.4, 0.9);
	runs[(u32)Experiment::TrivialPixelShader].changed = 0;
	limits = LimitsOf(runs);
	CHECK(limits.pixelShading < 0 && limits.rasterization < 0 && Near(limits.vertices, 0.4));
	// Under kShortestMs the timer's noise is all there is.
	Runs(runs, 0.0004, 0.0001, 0.0001, 0);
	limits = LimitsOf(runs);
	CHECK(limits.tooShort && limits.limiter.empty() && limits.vertices < 0);
	JsonWriter out;
	out.BeginObject();
	WriteExperiments(out, runs, limits);
	out.EndObject();
	CHECK(out.Text().find("\"tooShort\":true") != std::string::npos && out.Text().find("\"id\":\"onePixel\"") != std::string::npos);
}

TEST(experiments_trivial_shaders_build) {
	const std::vector<TargetOutput> targets = {{0, 4, 0}, {1, 2, 2}, {3, 1, 1}};
	const std::string hlsl = TrivialPixelShaderHlsl(targets);
	CHECK(hlsl.find("uint2 target1 : SV_Target1;") != std::string::npos && hlsl.find("int target3 : SV_Target3;") != std::string::npos);
	std::vector<u8> code;
	std::string log;
	CHECK(CompileHlsl(hlsl, "main", "ps_5_0", HlslTarget::Dxbc, code, log));
	CHECK(CompileHlsl(TrivialPixelShaderHlsl({}), "main", "ps_5_0", HlslTarget::Dxbc, code, log));
	std::vector<u32> words;
	CHECK(AssembleSpirv(TrivialFragmentShader(targets), nullptr, 0, words, log));
	SpirvModule module;
	CHECK(ParseSpirv(words.data(), words.size(), module));
	CHECK(module.entries.size() == 1 && module.entries[0].model == 4 && module.entries[0].name == "main");
	CHECK(module.outputs.size() == 3);
	bool uvec2 = false;
	for (const SpirvInterface& output : module.outputs) uvec2 |= output.location == 1 && output.type == "uvec2";
	CHECK(uvec2);
}
