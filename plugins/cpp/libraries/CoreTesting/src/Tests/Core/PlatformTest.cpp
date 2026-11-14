#include "PlatformTest.h"

namespace Testing
{
	namespace
	{
		constexpr uint32 c_ReplacementCharacter = 0xFFFD;

		// "cafe" with an acute e, a euro sign and an emoji: two, three and four byte sequences.
		const char* const c_MixedUtf8 = "caf\xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80";

		// A wide character as its code unit, which the assertions print.
		uint32 Unit(wchar_t c) { return static_cast<uint32>(c); }
	}

	void PlatformTest::Utf8RoundTripsThroughWideText()
	{
		const std::wstring wide = Platform::Utf8ToWide(c_MixedUtf8);
		TEST_ASSERT_EQ(Unit(wide[3]), uint32(0xE9));
		TEST_ASSERT_EQ(Unit(wide[5]), uint32(0x20AC));
		TEST_ASSERT_EQ(Platform::WideToUtf8(wide.c_str()), Core::String(c_MixedUtf8));
		TEST_ASSERT_EQ(Platform::WideToUtf8(Platform::Utf8ToWide("plain").c_str()), Core::String("plain"));
		TEST_ASSERT(Platform::Utf8ToWide("").empty());
	}

	void PlatformTest::InvalidUtf8BecomesReplacementCharacters()
	{
		const std::wstring stray = Platform::Utf8ToWide("a\xff" "b");
		TEST_ASSERT_EQ(stray.size(), size_t(3));
		TEST_ASSERT_EQ(Unit(stray[1]), c_ReplacementCharacter);
		TEST_ASSERT_EQ(Unit(stray[2]), uint32('b'));
		const std::wstring cut = Platform::Utf8ToWide("x\xe2\x82");
		TEST_ASSERT(cut.size() >= 2);
		TEST_ASSERT_EQ(Unit(cut[0]), uint32('x'));
		TEST_ASSERT_EQ(Unit(cut[1]), c_ReplacementCharacter);
	}

	void PlatformTest::NullTextIsEmpty()
	{
		TEST_ASSERT(Platform::Utf8ToWide(nullptr).empty());
		TEST_ASSERT(Platform::WideToUtf8(nullptr).empty());
	}

	void PlatformTest::ApplicationPathIsThisProgram()
	{
		const fs::path program = Platform::GetApplicationPath();
		TEST_ASSERT(fs::is_regular_file(program));
		TEST_ASSERT_EQ(program.stem().string(), std::string("CoreTesting"));
		TEST_ASSERT(Platform::GetApplicationFolder() == program.parent_path());
	}

	void PlatformTest::ExecuteProcessCapturesWhatTheProgramPrints()
	{
		const std::string output = Platform::ExecuteProcess(Platform::GetApplicationPath().string(), { "-list-tests=1" });
		TEST_ASSERT(output.find("Discoverable tests:") != std::string::npos);
		TEST_ASSERT(output.find("Platform") != std::string::npos);
		TEST_ASSERT(Platform::ExecuteProcess((Platform::GetApplicationFolder() / "no-such-program").string(), {}).empty());
	}

	void PlatformTest::SystemCallReturnsTheCommandsExitCode()
	{
		const Core::String program = "\"" + Platform::GetApplicationPath().string() + "\"";
		TEST_ASSERT_EQ(Platform::SystemCall(program + " -list-tests=1", "", true), 0);
		TEST_ASSERT(Platform::SystemCall("\"" + (Platform::GetApplicationFolder() / "no-such-program").string() + "\"", "", true) != 0);
	}
}
