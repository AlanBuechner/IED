#include "RuntimeObjectsTest.h"
#include "Compiler/RuntimeObjects.h"
#include <algorithm>

namespace Testing
{
	using namespace Scripting;

	static bool Has(const std::vector<std::string>& args, const char* arg)
	{
		return std::find(args.begin(), args.end(), arg) != args.end();
	}

	void RuntimeObjectsTest::SourcesCompileOptimizedWithWarningsAsErrors()
	{
		// BUG-044 (runtime built at -O0) and BUG-098 (warnings printed on every build).
		const std::vector<std::string> args = RuntimeCompileCommand("clang.exe", "C:/rt", "C:/rt/GC/GCHeap.c", "C:/obj/GC_GCHeap.obj", "");
		TEST_ASSERT(Has(args, "-O2"));
		TEST_ASSERT(Has(args, "-Werror"));
		TEST_ASSERT(Has(args, "-I") && Has(args, "C:/rt"));
		TEST_ASSERT(!Has(args, "-std=c++20"));
		TEST_ASSERT(args.front() == "clang.exe" && args.back() == "C:/obj/GC_GCHeap.obj");
	}

	void RuntimeObjectsTest::CppSourcesCompileAsCpp20()
	{
		const std::vector<std::string> args = RuntimeCompileCommand("clang.exe", "C:/rt", "C:/rt/GC/GCCallbacks.cpp", "C:/obj/x.obj", "");
		TEST_ASSERT(Has(args, "-std=c++20") && Has(args, "-O2"));
	}

	// The MSVC runtime and SDK of setup.bat's kit, not a Visual Studio's (or none on a machine without one).
	void RuntimeObjectsTest::SourcesTakeTheWindowsSysroot()
	{
		const std::vector<std::string> kit = RuntimeCompileCommand("clang.exe", "C:/rt", "C:/rt/GC/GCHeap.c", "C:/obj/x.obj", "C:/IED/vendor/msvc");
		const auto flag = std::find(kit.begin(), kit.end(), "-Xmicrosoft-windows-sys-root");
		TEST_ASSERT(flag != kit.end() && flag + 1 != kit.end() && *(flag + 1) == "C:/IED/vendor/msvc");
		TEST_ASSERT(kit.back() == "C:/obj/x.obj");
		const std::vector<std::string> none = RuntimeCompileCommand("clang.exe", "C:/rt", "C:/rt/GC/GCHeap.c", "C:/obj/x.obj", "");
		TEST_ASSERT(!Has(none, "-Xmicrosoft-windows-sys-root"));
	}

	// BUG-231: an object is found by its source's bytes, never by the files' times.
	void RuntimeObjectsTest::ChangedBytesNameAnotherObject()
	{
		const uint64_t headers = RuntimeContentKey("#pragma once\n", 1);
		const uint64_t before = RuntimeContentKey("int f(void) { return 1; }", headers);
		const uint64_t after = RuntimeContentKey("int f(void) { return 2; }", headers);
		TEST_ASSERT(before != after);
		TEST_ASSERT(RuntimeObjectName("Reflect/ReflectIndex.c", before) != RuntimeObjectName("Reflect/ReflectIndex.c", after));
		TEST_ASSERT_EQ(before, RuntimeContentKey("int f(void) { return 1; }", headers));
		TEST_ASSERT_EQ(RuntimeContentKey("", 7), uint64_t(7));
	}

	void RuntimeObjectsTest::AChangedHeaderRenamesEveryObject()
	{
		const std::string source = "int f(void) { return 1; }";
		const uint64_t one = RuntimeContentKey(source, RuntimeContentKey("#define A 1\n", 1));
		const uint64_t two = RuntimeContentKey(source, RuntimeContentKey("#define A 2\n", 1));
		TEST_ASSERT(one != two);
	}

	void RuntimeObjectsTest::ObjectNamesKeepTheSourcePath()
	{
		TEST_ASSERT_EQ(RuntimeObjectName("GC/GCHeap.c", 0x1234), std::string("GC_GCHeap-0000000000001234.obj"));
		TEST_ASSERT_EQ(RuntimeObjectName("EHRuntime.cpp", 0), std::string("EHRuntime-0000000000000000.obj"));
	}
} // namespace Testing
