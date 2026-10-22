// experiments.cpp - experiments' shares and the trivial pixel shader (experiments.h).
#include "experiments.h"

#include <algorithm>

namespace fb
{
	namespace
	{
		double Share(double saved, double captured) { return std::clamp(saved / captured, 0.0, 1.0); }
	}

	const char* ExperimentName(Experiment experiment) {
		switch (experiment) {
			case Experiment::Captured: return "captured";
			case Experiment::TrivialPixelShader: return "trivialPixelShader";
			case Experiment::OnePixel: return "onePixel";
			case Experiment::NoColorWrites: return "noColorWrites";
			default: return "?";
		}
	}

	Limits LimitsOf(const ExperimentRun (&runs)[kExperiments]) {
		Limits limits;
		const double captured = runs[(u32)Experiment::Captured].ms;
		if (captured < 0) return limits;
		if (captured < kShortestMs) {
			limits.tooShort = true;
			return limits;
		}
		const ExperimentRun& trivial = runs[(u32)Experiment::TrivialPixelShader];
		const ExperimentRun& onePixel = runs[(u32)Experiment::OnePixel];
		const ExperimentRun& noWrites = runs[(u32)Experiment::NoColorWrites];
		const bool hasTrivial = trivial.ms >= 0 && trivial.changed, hasOnePixel = onePixel.ms >= 0 && onePixel.changed, hasNoWrites = noWrites.ms >= 0 && noWrites.changed;
		if (hasOnePixel) limits.vertices = Share(onePixel.ms, captured);
		if (hasTrivial) limits.pixelShading = Share(captured - trivial.ms, captured);
		if (hasTrivial && hasOnePixel) limits.rasterization = Share(trivial.ms - onePixel.ms, captured);
		if (hasNoWrites) limits.colorOutput = Share(captured - noWrites.ms, captured);
		double largest = -1;
		for (const auto& share : {std::make_pair("vertices", limits.vertices), std::make_pair("pixelShading", limits.pixelShading),
				 std::make_pair("rasterization", limits.rasterization), std::make_pair("colorOutput", limits.colorOutput)})
			if (share.second > largest) {
				largest = share.second;
				limits.limiter = share.first;
			}
		if (largest < 0) limits.limiter.clear();
		return limits;
	}

	void WriteExperiments(JsonWriter& out, const ExperimentRun (&runs)[kExperiments], const Limits& limits) {
		out.BeginArray("variants");
		for (u32 e = 0; e < kExperiments; ++e) {
			out.BeginObject().String("id", ExperimentName((Experiment)e));
			if (runs[e].ms >= 0) out.Double("ms", runs[e].ms);
			else out.Null("ms");
			out.Uint("changed", runs[e].changed);
			if (!runs[e].problem.empty()) out.String("problem", runs[e].problem);
			out.EndObject();
		}
		out.EndArray();
		out.BeginObject("shares");
		for (const auto& share : {std::make_pair("vertices", limits.vertices), std::make_pair("pixelShading", limits.pixelShading),
				 std::make_pair("rasterization", limits.rasterization), std::make_pair("colorOutput", limits.colorOutput)}) {
			if (share.second >= 0) out.Double(share.first, share.second);
			else out.Null(share.first);
		}
		out.EndObject();
		out.String("limiter", limits.limiter);
		out.Bool("tooShort", limits.tooShort);
	}

	std::string TrivialPixelShaderHlsl(const std::vector<TargetOutput>& targets) {
		if (targets.empty()) return "void main()\n{\n}\n";
		std::string text = "struct Outputs\n{\n";
		for (const TargetOutput& t : targets) {
			const char* kind = t.kind == 1 ? "int" : t.kind == 2 ? "uint" : "float";
			text += Format("\t%s%s target%u : SV_Target%u;\n", kind, t.components > 1 ? std::to_string(t.components).c_str() : "", t.reg, t.reg);
		}
		return text + "};\n\nOutputs main()\n{\n\tOutputs outputs = (Outputs)0;\n\treturn outputs;\n}\n";
	}
}
