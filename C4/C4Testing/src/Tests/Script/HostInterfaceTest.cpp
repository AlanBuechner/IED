#include "HostInterfaceTest.h"
#include "Script/HostInterface.h"
#include <filesystem>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		HostInterface Sample()
		{
			HostInterface host;
			for (uint8 i = 0; i < 16; ++i) host.buildId[i] = static_cast<uint8>(i * 17 + 3);
			host.compilerVersion = 1234;
			host.targetCpu = "x86-64-v3";
			host.targetFeatures = "+avx2,+fma,-avx512f";
			host.debugInfo = true;
			host.projects.push_back({ "engine", {}, { 0 } });
			host.projects.push_back({ "game", { 0 }, { 1, 2 } });
			host.folderCount = 3;
			InterfaceFile header;
			header.unitId = 1;
			header.project = 1;
			header.folder = 2;
			header.path = "ai/brain.ch";
			header.text = "int32 Think(int32 depth) ;\n";
			MacroDefinition object;
			object.name = "MAX_DEPTH";
			object.body = "16";
			MacroDefinition function;
			function.name = "SQUARE";
			function.isFunctionLike = true;
			function.params = { "x", "..." };
			function.body = "((x) * (x))";
			header.macros = { object, function };
			InterfaceFile prelude;
			prelude.unitId = 2;
			prelude.path = "Prelude/prelude.ch";
			prelude.library = true;
			prelude.prelude = true;
			prelude.text = std::string("with a \0 byte", 13);
			host.files = { header, prelude };
			host.symbols = { "Think_1int32", "g_score", "c4_alloc" };
			host.reflectedTypes = { "Ai::Brain", "Box<int32>" };
			host.inlineBodies = { std::string("BC\xC0\xDE\0\x01", 6), std::string("BC\xC0\xDE\0\x02", 6) };
			return host;
		}
	}

	void HostInterfaceTest::EveryFieldRoundTrips()
	{
		const HostInterface written = Sample();
		HostInterface read;
		std::string error;
		TEST_ASSERT(DeserializeInterface(SerializeInterface(written), read, &error));
		TEST_ASSERT(read.buildId == written.buildId);
		TEST_ASSERT_EQ(read.compilerVersion, written.compilerVersion);
		TEST_ASSERT(read.targetCpu == "x86-64-v3" && read.targetFeatures == "+avx2,+fma,-avx512f");
		TEST_ASSERT(read.debugInfo); // the host was built with -g: its scripts are debugged at first
		TEST_ASSERT_EQ(read.projects.size(), (size_t)2);
		TEST_ASSERT(read.projects[1].name == "game" && read.projects[1].dependencies == std::vector<uint32>{ 0 });
		TEST_ASSERT(read.projects[1].folders == (std::vector<uint32>{ 1, 2 }));
		TEST_ASSERT_EQ(read.folderCount, (uint32)3);
		TEST_ASSERT_EQ(read.files.size(), (size_t)2);
		const InterfaceFile& header = read.files[0];
		TEST_ASSERT(header.unitId == 1 && header.project == 1 && header.folder == 2 && header.path == "ai/brain.ch");
		TEST_ASSERT(header.text == "int32 Think(int32 depth) ;\n" && !header.library && !header.prelude);
		TEST_ASSERT_EQ(header.macros.size(), (size_t)2);
		TEST_ASSERT(header.macros[1].isFunctionLike && header.macros[1].params == (std::vector<std::string>{ "x", "..." }));
		TEST_ASSERT(header.macros[1].body == "((x) * (x))" && header.macros[0].body == "16");
		TEST_ASSERT(read.files[1].library && read.files[1].prelude && read.files[1].folder == -1);
		TEST_ASSERT(read.files[1].text == written.files[1].text);
		TEST_ASSERT(read.symbols == written.symbols);
		TEST_ASSERT(read.reflectedTypes == written.reflectedTypes);
		TEST_ASSERT(read.inlineBodies == written.inlineBodies);
		TEST_PASSED();
	}

	void HostInterfaceTest::AnotherFormatIsRefused()
	{
		HostInterface read;
		std::string error;
		TEST_ASSERT(!DeserializeInterface("MZ\x90\0 not an interface", read, &error));
		TEST_ASSERT(error.find("not a C4 interface") != std::string::npos);
		TEST_PASSED();
	}

	void HostInterfaceTest::AnotherVersionIsRefused()
	{
		std::string bytes = SerializeInterface(Sample());
		bytes[4] = 99; // the format version follows the magic
		HostInterface read;
		std::string error;
		TEST_ASSERT(!DeserializeInterface(bytes, read, &error));
		TEST_ASSERT(error.find("format 99") != std::string::npos);
		TEST_PASSED();
	}

	void HostInterfaceTest::AShortFileIsRefused()
	{
		const std::string bytes = SerializeInterface(Sample());
		for (size_t cut : { size_t(10), bytes.size() / 2, bytes.size() - 1 })
		{
			HostInterface read;
			std::string error;
			TEST_ASSERT(!DeserializeInterface(bytes.substr(0, cut), read, &error));
			TEST_ASSERT(error.find("ends early") != std::string::npos);
		}
		TEST_PASSED();
	}

	void HostInterfaceTest::AnUnknownTargetIsRefused()
	{
		HostInterface unknown = Sample();
		unknown.targetCpu = "abacus";
		HostInterface read;
		std::string error;
		TEST_ASSERT(!DeserializeInterface(SerializeInterface(unknown), read, &error));
		TEST_ASSERT(error.find("the host's target: 'abacus' names no x86-64 processor") != std::string::npos);
		TEST_PASSED();
	}

	void HostInterfaceTest::TrailingBytesAreRefused()
	{
		HostInterface read;
		std::string error;
		TEST_ASSERT(!DeserializeInterface(SerializeInterface(Sample()) + "x", read, &error));
		TEST_ASSERT(error.find("past its end") != std::string::npos);
		TEST_PASSED();
	}

	void HostInterfaceTest::WritesAndReadsAFile()
	{
		const std::filesystem::path folder = std::filesystem::temp_directory_path() / "c4_host_interface_test";
		std::filesystem::create_directories(folder);
		const std::string path = (folder / "game.c4i").string();
		std::string error;
		TEST_ASSERT(WriteInterface(Sample(), path, &error));
		HostInterface read;
		TEST_ASSERT(ReadInterface(path, read, &error));
		TEST_ASSERT(read.symbols == Sample().symbols && read.files.size() == 2);
		TEST_PASSED();
	}

	void HostInterfaceTest::AMissingFileIsAnError()
	{
		HostInterface read;
		std::string error;
		const std::string missing = (std::filesystem::temp_directory_path() / "c4_host_interface_no_such_folder" / "none.c4i").string();
		TEST_ASSERT(!ReadInterface(missing, read, &error));
		TEST_ASSERT(error.find("cannot read") != std::string::npos);
		TEST_PASSED();
	}
} // namespace Testing
