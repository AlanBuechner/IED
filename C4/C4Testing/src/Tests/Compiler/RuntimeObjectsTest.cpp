#include "RuntimeObjectsTest.h"
#include "Compiler/RuntimeObjects.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <llvm/Support/Process.h>
#include <sstream>

namespace Testing
{
	using namespace C4Compiler;
	namespace fs = std::filesystem;

	namespace
	{
		// A process that runs whoever asks: Windows' System process, Linux's init.
#ifdef _WIN32
		const std::string kAlwaysRunning = "4";
#else
		const std::string kAlwaysRunning = "1";
#endif

		fs::path FreshFolder(const char* name)
		{
			const fs::path folder = fs::temp_directory_path() / "c4-runtimeobjects-test" / name;
			fs::remove_all(folder);
			fs::create_directories(folder);
			return folder;
		}

		void Write(const fs::path& file, const std::string& text) { std::ofstream(file, std::ios::binary) << text; }

		std::string Read(const fs::path& file)
		{
			std::ostringstream text;
			text << std::ifstream(file, std::ios::binary).rdbuf();
			return text.str();
		}

		// What KeepRuntimeObjects asks of a compile: the object written, counted.
		struct FakeCompiler
		{
			std::atomic<int> compiled = 0;

			bool operator()(const RuntimeObject& object)
			{
				Write(object.object, "object of " + object.source.generic_string());
				++compiled;
				return true;
			}
		};

		std::function<bool(const RuntimeObject&)> Calls(FakeCompiler& compiler)
		{
			return [&compiler](const RuntimeObject& object) { return compiler(object); };
		}
	}

	static bool Has(const std::vector<std::string>& args, const char* arg)
	{
		return std::find(args.begin(), args.end(), arg) != args.end();
	}

	static const TargetAbi& Windows() { return TargetAbi::For(TargetSystem::Windows); }
	static const TargetAbi& Linux() { return TargetAbi::For(TargetSystem::Linux); }

	void RuntimeObjectsTest::SourcesCompileOptimizedWithWarningsAsErrors()
	{
		// BUG-044 (runtime built at -O0) and BUG-098 (warnings printed on every build).
		const std::vector<std::string> args = RuntimeCompileCommand("clang.exe", "C:/rt", "C:/rt/GC/GCHeap.c", "C:/obj/GC_GCHeap.obj", Windows(), "");
		TEST_ASSERT(Has(args, "-O2"));
		TEST_ASSERT(Has(args, "-Werror"));
		TEST_ASSERT(Has(args, "-I") && Has(args, "C:/rt"));
		TEST_ASSERT(!Has(args, "-std=c++20"));
		TEST_ASSERT(args.front() == "clang.exe" && args.back() == "C:/obj/GC_GCHeap.obj");
	}

	void RuntimeObjectsTest::CppSourcesCompileAsCpp20()
	{
		const std::vector<std::string> args = RuntimeCompileCommand("clang.exe", "C:/rt", "C:/rt/GC/GCCallbacks.cpp", "C:/obj/x.obj", Windows(), "");
		TEST_ASSERT(Has(args, "-std=c++20") && Has(args, "-O2"));
	}

	// The MSVC runtime and SDK of setup.bat's kit, not a Visual Studio's (or none on a machine without one).
	void RuntimeObjectsTest::SourcesTakeTheWindowsSysroot()
	{
		const std::vector<std::string> kit = RuntimeCompileCommand("clang.exe", "C:/rt", "C:/rt/GC/GCHeap.c", "C:/obj/x.obj", Windows(), "C:/IED/vendor/msvc");
		const auto flag = std::find(kit.begin(), kit.end(), "-Xmicrosoft-windows-sys-root");
		TEST_ASSERT(flag != kit.end() && flag + 1 != kit.end() && *(flag + 1) == "C:/IED/vendor/msvc");
		TEST_ASSERT(kit.back() == "C:/obj/x.obj");
		const std::vector<std::string> none = RuntimeCompileCommand("clang.exe", "C:/rt", "C:/rt/GC/GCHeap.c", "C:/obj/x.obj", Windows(), "");
		TEST_ASSERT(!Has(none, "-Xmicrosoft-windows-sys-root"));
	}

	// A Linux program's runtime (D-86): glibc's headers from the sysroot, position independent, for the Linux triple.
	void RuntimeObjectsTest::LinuxSourcesTakeTheLinuxSysroot()
	{
		const std::vector<std::string> args = RuntimeCompileCommand("clang.exe", "C:/rt", "C:/rt/GC/GCHeap.c", "C:/obj/x.obj", Linux(), "C:/IED/vendor/sysroots/linux-x64");
		TEST_ASSERT(Has(args, "--sysroot=C:/IED/vendor/sysroots/linux-x64"));
		TEST_ASSERT(Has(args, "x86_64-unknown-linux-gnu") && Has(args, "-fPIC"));
		TEST_ASSERT(!Has(args, "x86_64-pc-windows-msvc") && !Has(args, "-Xmicrosoft-windows-sys-root"));
		TEST_ASSERT(Has(args, "-O2") && Has(args, "-Werror"));
	}

	// Each system's own platform files; the rest are both systems'.
	void RuntimeObjectsTest::EachSystemCompilesItsOwnPlatformFiles()
	{
		TEST_ASSERT(RuntimeSourceIsFor("GC/GCPlatformWin32.c", Windows()));
		TEST_ASSERT(!RuntimeSourceIsFor("GC/GCPlatformWin32.c", Linux()));
		TEST_ASSERT(RuntimeSourceIsFor("GC/GCPlatformPosix.c", Linux()));
		TEST_ASSERT(!RuntimeSourceIsFor("GC/GCPlatformPosix.c", Windows()));
		TEST_ASSERT(!RuntimeSourceIsFor("FileSystem/WatchLinux.c", Windows()));
		TEST_ASSERT(RuntimeSourceIsFor("GC/GCHeap.c", Windows()) && RuntimeSourceIsFor("GC/GCHeap.c", Linux()));
		TEST_ASSERT(RuntimeSourceIsFor("Script/ScriptExitsC.c", Windows()));
		TEST_ASSERT(RuntimeSourceIsFor("Script/ScriptExitsC.c", Linux())); // a Linux script host's too
		TEST_ASSERT(!RuntimeSourceIsFor("Script/ScriptThrowWin32.c", Linux()));
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

	// BUG-510: a compiler of another runtime version (its sources' bytes changed while it ran) listed its objects and has not
	// linked yet; this build compiles its own version of the same sources. The running build's objects stay, an ended
	// build's other version goes, and this build's are there to link.
	void RuntimeObjectsTest::Bug510AnotherRunningBuildsVersionStays()
	{
		const fs::path folder = FreshFolder("running-version");
		const fs::path running = folder / RuntimeObjectName("TextRuntime.c", 0xA);
		const fs::path ended = folder / RuntimeObjectName("TextRuntime.c", 0xC);
		Write(running, "another build's");
		Write(ended, "an ended build's");
		Write(folder / (".claim." + kAlwaysRunning), running.filename().string() + "\n");
		// What the running build's clang is writing: its temporary of the private file it renames into place.
		const fs::path writing = folder / (RuntimeObjectName("IORuntimeC.c", 0xA) + "." + kAlwaysRunning + "-e962f501.tmp.tmp");
		Write(writing, "half");
		const std::vector<RuntimeObject> ours = { { "rt/TextRuntime.c", folder / RuntimeObjectName("TextRuntime.c", 0xB) },
			{ "rt/GC/GCHeap.c", folder / RuntimeObjectName("GC/GCHeap.c", 0xB) } };
		FakeCompiler compiler;

		TEST_ASSERT(KeepRuntimeObjects(folder, ours, Calls(compiler)));

		TEST_ASSERT_EQ(compiler.compiled.load(), 2);
		TEST_ASSERT(fs::exists(ours[0].object) && fs::exists(ours[1].object));
		TEST_ASSERT_EQ(Read(running), std::string("another build's"));
		TEST_ASSERT(fs::exists(writing));
		TEST_ASSERT(!fs::exists(ended));
	}

	// A build claims its objects before it looks for them: another build that prunes afterwards spares them, though they
	// are not its version; one already there is not compiled again.
	void RuntimeObjectsTest::Bug510ObjectsAreClaimedBeforeTheyAreLookedFor()
	{
		const fs::path folder = FreshFolder("claimed");
		const std::vector<RuntimeObject> ours = { { "rt/TextRuntime.c", folder / RuntimeObjectName("TextRuntime.c", 0xA) } };
		Write(ours[0].object, "kept");
		FakeCompiler compiler;

		TEST_ASSERT(KeepRuntimeObjects(folder, ours, Calls(compiler)));

		TEST_ASSERT_EQ(compiler.compiled.load(), 0);
		TEST_ASSERT_EQ(Read(ours[0].object), std::string("kept"));
		const fs::path claims = folder / (".claim." + std::to_string(llvm::sys::Process::getProcessId()));
		TEST_ASSERT(Read(claims).find(ours[0].object.filename().string()) != std::string::npos);
	}

	// An object another build removed after this one found it (before this one's claim could be read) is compiled again.
	void RuntimeObjectsTest::Bug510AnObjectRemovedAfterItWasFoundIsCompiledAgain()
	{
		const fs::path folder = FreshFolder("vanished");
		const std::vector<RuntimeObject> ours = { { "rt/TextRuntime.c", folder / RuntimeObjectName("TextRuntime.c", 0xA) },
			{ "rt/GC/GCHeap.c", folder / RuntimeObjectName("GC/GCHeap.c", 0xA) } };
		Write(ours[0].object, "found");
		std::atomic<int> compiled = 0;
		const auto compile = [&](const RuntimeObject& object) {
			if (compiled++ == 0) fs::remove(ours[0].object); // another build's pruning, while this one compiles GCHeap
			Write(object.object, "compiled");
			return true;
		};

		TEST_ASSERT(KeepRuntimeObjects(folder, ours, compile));

		TEST_ASSERT_EQ(compiled.load(), 2);
		TEST_ASSERT_EQ(Read(ours[0].object), std::string("compiled"));
		TEST_ASSERT(fs::exists(ours[1].object));
	}

	// A failed compile fails the build; nothing is pruned for it.
	void RuntimeObjectsTest::AFailedCompileFails()
	{
		const fs::path folder = FreshFolder("failed");
		const fs::path other = folder / RuntimeObjectName("TextRuntime.c", 0xC);
		Write(other, "an ended build's");
		const std::vector<RuntimeObject> ours = { { "rt/TextRuntime.c", folder / RuntimeObjectName("TextRuntime.c", 0xA) } };

		TEST_ASSERT(!KeepRuntimeObjects(folder, ours, [](const RuntimeObject&) { return false; }));

		TEST_ASSERT(fs::exists(other));
	}
} // namespace Testing
