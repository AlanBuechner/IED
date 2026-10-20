#include "ForeignCacheTest.h"
#include "Interop/ForeignCache.h"
#include <filesystem>
#include <fstream>
#include <string>

namespace Testing
{
	using namespace Scripting;
	namespace fs = std::filesystem;

	namespace
	{
		void Write(const fs::path& path, const std::string& text)
		{
			std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
		}

		// A source including a header, a stand-in compiler, and an object with the depfile a compile of it writes.
		struct Fixture
		{
			fs::path folder;
			ForeignObject object;
			std::string compiler;

			Fixture()
			{
				folder = fs::temp_directory_path() / "c4_foreign_cache_test";
				fs::remove_all(folder);
				fs::create_directories(folder);
				Write(folder / "util.c", "#include \"util.h\"\nint util(void) { return UTIL; }\n");
				Write(folder / "util.h", "#define UTIL 1\n");
				Write(folder / "clang.exe", "compiler");
				object.srcPath = (folder / "util.c").string();
				object.objPath = (folder / "util.obj").string();
				object.compileFlags = { "-O2" };
				compiler = (folder / "clang.exe").string();
				Compile();
			}
			~Fixture() { std::error_code ignored; fs::remove_all(folder, ignored); }

			void Compile()
			{
				Write(object.objPath, "object");
				Write(object.objPath + ".d", "util.obj: " + object.srcPath + " \\\n  " + (folder / "util.h").string() + "\n");
				RecordForeignObject(object, compiler);
			}
		};
	}

	void ForeignCacheTest::DepfileInputsReadMakeSyntax()
	{
		const std::vector<std::string> inputs = DepfileInputs("out/a.obj: C:\\src\\a\\ b.c \\\n  C:\\inc\\a.h \\\r\n  b.h\n");
		TEST_ASSERT(inputs.size() == 3);
		TEST_ASSERT(inputs[0] == "C:\\src\\a b.c");
		TEST_ASSERT(inputs[1] == "C:\\inc\\a.h");
		TEST_ASSERT(inputs[2] == "b.h");
		TEST_ASSERT(DepfileInputs("").empty());
	}

	void ForeignCacheTest::AnObjectIsCurrentUntilAnInputChanges()
	{
		Fixture fixture;
		TEST_ASSERT(ForeignObjectIsCurrent(fixture.object, fixture.compiler));
		Write(fixture.folder / "util.h", "#define UTIL 2\n");
		TEST_ASSERT(!ForeignObjectIsCurrent(fixture.object, fixture.compiler)); // a header it includes changed
		fixture.Compile();
		TEST_ASSERT(ForeignObjectIsCurrent(fixture.object, fixture.compiler));
		Write(fixture.folder / "util.c", "int util(void) { return 3; }\n");
		TEST_ASSERT(!ForeignObjectIsCurrent(fixture.object, fixture.compiler)); // its source changed
		fixture.Compile();
		fs::remove(fixture.folder / "util.h");
		TEST_ASSERT(!ForeignObjectIsCurrent(fixture.object, fixture.compiler)); // a header it read is gone
	}

	void ForeignCacheTest::FlagsAndTheCompilerArePartOfTheKey()
	{
		Fixture fixture;
		ForeignObject debug = fixture.object;
		debug.compileFlags = { "-O0", "-g" };
		TEST_ASSERT(!ForeignObjectIsCurrent(debug, fixture.compiler));
		ForeignObject defined = fixture.object;
		defined.defines = { "EXTRA=1" };
		TEST_ASSERT(!ForeignObjectIsCurrent(defined, fixture.compiler));
		Write(fixture.folder / "clang.exe", "a newer compiler");
		TEST_ASSERT(!ForeignObjectIsCurrent(fixture.object, fixture.compiler));
	}

	void ForeignCacheTest::AnObjectWithoutItsRecordIsNotCurrent()
	{
		Fixture fixture;
		fs::remove(fixture.object.objPath + ".key");
		TEST_ASSERT(!ForeignObjectIsCurrent(fixture.object, fixture.compiler)); // compiled, never recorded
		fixture.Compile();
		fs::remove(fixture.object.objPath + ".d");
		TEST_ASSERT(!ForeignObjectIsCurrent(fixture.object, fixture.compiler)); // no depfile: what it read is unknown
		fixture.Compile();
		fs::remove(fixture.object.objPath);
		TEST_ASSERT(!ForeignObjectIsCurrent(fixture.object, fixture.compiler)); // no object
	}
} // namespace Testing
