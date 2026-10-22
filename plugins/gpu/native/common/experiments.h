// experiments.h - a pass's or draw's limiter found by experiments (docs/22 "Performance"): with no vendor counters, the
// replay times the range as captured, then with every draw in it changed - its pixel shader made trivial, its scissor
// cut to one pixel, its color writes off - and what each change saves says where the time goes: vertex and primitive
// work, pixel shading, rasterization and depth, or color output.
#pragma once

#include "json.h"

namespace fb
{
	enum class Experiment : u32 { Captured, TrivialPixelShader, OnePixel, NoColorWrites, Count };
	constexpr u32 kExperiments = (u32)Experiment::Count;
	// "captured", "trivialPixelShader", "onePixel", "noColorWrites".
	const char* ExperimentName(Experiment experiment);

	struct ExperimentRun
	{
		double ms = -1;          // the range's median GPU time; below 0 when it did not run
		u32 changed = 0;         // the draws the experiment changed
		std::string problem;     // why a draw was left as captured
	};

	// Shares of the captured time, each below 0 when unknown: what is left when no pixel is drawn (vertex and primitive
	// work), what a trivial pixel shader saves (pixel shading), what is between the two (rasterization, depth and the
	// writes of a trivial shader), what turning color writes off saves (color output: blending, bandwidth). The limiter
	// is the largest; they overlap and need not add up to one.
	struct Limits
	{
		double vertices = -1, pixelShading = -1, rasterization = -1, colorOutput = -1;
		std::string limiter;     // "vertices", "pixelShading", "rasterization", "colorOutput"; "" when none is known
		bool tooShort = false;   // the range took under kShortestMs: what the experiments save is the timer's noise
	};
	constexpr double kShortestMs = 0.002;

	Limits LimitsOf(const ExperimentRun (&runs)[kExperiments]);
	// `variants` ([{id, ms, changed, problem?}]), `shares` and `limiter` into an answer.
	void WriteExperiments(JsonWriter& out, const ExperimentRun (&runs)[kExperiments], const Limits& limits);

	// A render target a pixel shader writes: its register, components and kind (0 float, 1 int, 2 uint).
	struct TargetOutput
	{
		u32 reg = 0;
		u32 components = 4;
		u32 kind = 0;
	};

	// HLSL of a pixel shader that writes zero to each of `targets`, entry `main`, taking no inputs.
	std::string TrivialPixelShaderHlsl(const std::vector<TargetOutput>& targets);
}
