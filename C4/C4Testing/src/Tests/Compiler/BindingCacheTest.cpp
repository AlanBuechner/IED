#include "BindingCacheTest.h"
#include "Interop/InteropPass.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;
	namespace fs = std::filesystem;

	namespace
	{
		std::vector<fs::path> BindingsIn(const fs::path& folder)
		{
			std::vector<fs::path> bindings;
			for (const auto& entry : fs::directory_iterator(folder))
				if (entry.path().extension() == ".c4bind") bindings.push_back(entry.path());
			return bindings;
		}
	}

	void BindingCacheTest::EachWayOfParsingAHeaderKeepsItsBinding()
	{
		const fs::path folder = fs::temp_directory_path() / "c4_binding_cache_test";
		std::error_code ignored;
		fs::remove_all(folder, ignored);
		for (const char* sub : { "obj", "a", "b" }) fs::create_directories(folder / sub);
		std::ofstream(folder / "lib.h", std::ios::binary) << "int lib_twice(int v);\n";
		const std::string objDir = (folder / "obj").generic_string();
		const InteropFileInput file{ "main.cf", (folder / "main.cf").generic_string(), "extern \"c\" include \"lib.h\"\n", {} };
		const auto bind = [&](const char* includeDir) {
			return InteropPass::RunFolder({ folder.generic_string() }, { file }, { (folder / includeDir).generic_string() }, objDir, {}, false, true);
		};

		const InteropPass::Result first = bind("a");
		TEST_ASSERT(first.errors.empty());
		TEST_ASSERT_EQ(BindingsIn(folder / "obj").size(), (size_t)1);
		const fs::path binding = BindingsIn(folder / "obj")[0];
		const auto written = fs::last_write_time(binding);

		TEST_ASSERT(bind("b").errors.empty()); // another program's folders: a binding of its own
		TEST_ASSERT_EQ(BindingsIn(folder / "obj").size(), (size_t)2);
		TEST_ASSERT(bind("a").errors.empty()); // the first program's again: its binding is still there
		TEST_ASSERT(fs::last_write_time(binding) == written);
		TEST_ASSERT_EQ(BindingsIn(folder / "obj").size(), (size_t)2);
		fs::remove_all(folder, ignored);
		TEST_PASSED();
	}

	void BindingCacheTest::AHeaderTheBoundOneIncludesChangesTheBinding()
	{
		const fs::path folder = fs::temp_directory_path() / "c4_binding_cache_includes_test";
		std::error_code ignored;
		fs::remove_all(folder, ignored);
		fs::create_directories(folder / "obj");
		std::ofstream(folder / "inner.h", std::ios::binary) << "int inner_one(void);\n";
		std::ofstream(folder / "outer.h", std::ios::binary) << "#include \"inner.h\"\nint outer(void);\n";
		const std::string objDir = (folder / "obj").generic_string();
		const InteropFileInput file{ "main.cf", (folder / "main.cf").generic_string(), "extern \"c\" include \"outer.h\"\n", {} };
		const auto bind = [&] { return InteropPass::RunFolder({ folder.generic_string() }, { file }, {}, objDir, {}, false, true); };
		const auto bound = [&](const char* name) {
			const InteropPass::Result result = bind();
			return result.errors.empty() && result.files.size() == 1 && result.files[0].text.find(name) != std::string::npos;
		};

		TEST_ASSERT(bound("inner_one"));
		std::ofstream(folder / "inner.h", std::ios::binary | std::ios::trunc) << "int inner_two(void);\nint inner_three(void);\n";
		TEST_ASSERT(bound("inner_two")); // made again: what the binding read changed
		fs::remove_all(folder, ignored);
		TEST_PASSED();
	}
} // namespace Testing
