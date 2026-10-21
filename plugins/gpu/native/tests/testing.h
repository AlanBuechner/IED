// testing.h - the unit tests' registry: TEST(name) { CHECK(...); }
#pragma once

#include "fb_base.h"

namespace fb::testing
{
	struct Context
	{
		int failures = 0;
		void Check(bool condition, const char* text, const char* file, int line);
	};

	struct Test
	{
		const char* name;
		void (*body)(Context&);
	};

	std::vector<Test>& Registry();

	struct Registrar
	{
		Registrar(const char* name, void (*body)(Context&));
	};
}

#define TEST(name)                                                                \
	static void Test_##name(fb::testing::Context& context);                       \
	static fb::testing::Registrar Registrar_##name(#name, Test_##name);           \
	static void Test_##name(fb::testing::Context& context)

#define CHECK(condition) context.Check((condition), #condition, __FILE__, __LINE__)
