#include "DapFormattingTest.h"
#include "Target/Launch.h"
#include "Target/OutputPipes.h"
#include "Target/ScalarText.h"
#include "Target/ThrownType.h"
#include <array>
#include <cstring>
#include <shellapi.h>

using namespace C4DAP::Target;

namespace Testing
{
	namespace
	{
		template <typename T> std::string Scalar(BasicKind kind, T value)
		{
			uint8_t bytes[sizeof(T)];
			std::memcpy(bytes, &value, sizeof(T));
			return ScalarText(kind, sizeof(T), bytes);
		}

		// How the C runtime splits a command line back into arguments (CommandLineToArgvW follows the same rules).
		std::vector<std::wstring> Split(const std::wstring& line)
		{
			int count = 0;
			LPWSTR* parts = CommandLineToArgvW(line.c_str(), &count);
			std::vector<std::wstring> split(parts, parts + count);
			LocalFree(parts);
			return split;
		}
	}

	void DapFormattingTest::IntegersAreDecimalAndSignExtended()
	{
		TEST_ASSERT_EQ(Scalar(BasicKind::Signed, int8_t(-5)), std::string("-5"));
		TEST_ASSERT_EQ(Scalar(BasicKind::Signed, int16_t(-300)), std::string("-300"));
		TEST_ASSERT_EQ(Scalar(BasicKind::Signed, int32_t(INT32_MIN)), std::string("-2147483648"));
		TEST_ASSERT_EQ(Scalar(BasicKind::Signed, int64_t(INT64_MIN)), std::string("-9223372036854775808"));
		TEST_ASSERT_EQ(Scalar(BasicKind::Unsigned, uint8_t(255)), std::string("255"));
		TEST_ASSERT_EQ(Scalar(BasicKind::Unsigned, uint64_t(UINT64_MAX)), std::string("18446744073709551615"));
		TEST_ASSERT_EQ(Scalar(BasicKind::Bool, uint8_t(1)), std::string("true"));
		TEST_ASSERT_EQ(Scalar(BasicKind::Bool, uint8_t(0)), std::string("false"));
		TEST_ASSERT_EQ(IntegerValue(BasicKind::Signed, 1, std::array<uint8_t, 1>{ 0xFF }.data()), int64_t(-1));
		TEST_ASSERT_EQ(IntegerValue(BasicKind::Unsigned, 1, std::array<uint8_t, 1>{ 0xFF }.data()), int64_t(255));
		TEST_PASSED();
	}

	void DapFormattingTest::FloatsAreShortestExact()
	{
		TEST_ASSERT_EQ(Scalar(BasicKind::Float, 0.5f), std::string("0.5"));
		TEST_ASSERT_EQ(Scalar(BasicKind::Float, 0.1), std::string("0.1"));
		TEST_ASSERT_EQ(Scalar(BasicKind::Float, 2.5), std::string("2.5"));
		TEST_ASSERT_EQ(Scalar(BasicKind::Float, -1e300), std::string("-1e+300"));
		uint8_t odd[2] = { 0, 0 };
		TEST_ASSERT_EQ(ScalarText(BasicKind::Float, 2, odd), std::string("?"));
		TEST_PASSED();
	}

	void DapFormattingTest::CharactersShowTheirCodeAndGlyph()
	{
		TEST_ASSERT_EQ(Scalar(BasicKind::Char8, uint8_t('a')), std::string("97 'a'"));
		TEST_ASSERT_EQ(Scalar(BasicKind::Char8, uint8_t('\n')), std::string("10"));
		TEST_ASSERT_EQ(Scalar(BasicKind::Char16, uint16_t(0x263A)), std::string("9786"));
		TEST_ASSERT_EQ(Scalar(BasicKind::Char32, uint32_t('Z')), std::string("90 'Z'"));
		TEST_PASSED();
	}

	void DapFormattingTest::BasicTypesUseC4Names()
	{
		TEST_ASSERT_EQ(BasicTypeName(BasicKind::Signed, 4), std::string("int32"));
		TEST_ASSERT_EQ(BasicTypeName(BasicKind::Unsigned, 1), std::string("uint8"));
		TEST_ASSERT_EQ(BasicTypeName(BasicKind::Unsigned, 8), std::string("uint64"));
		TEST_ASSERT_EQ(BasicTypeName(BasicKind::Float, 4), std::string("float"));
		TEST_ASSERT_EQ(BasicTypeName(BasicKind::Float, 8), std::string("double"));
		TEST_ASSERT_EQ(BasicTypeName(BasicKind::Char8, 1), std::string("char8"));
		TEST_ASSERT_EQ(BasicTypeName(BasicKind::Bool, 1), std::string("bool"));
		TEST_ASSERT_EQ(AddressText(0x1234), std::string("0x0000000000001234"));
		TEST_PASSED();
	}

	void DapFormattingTest::StringsAreQuotedAndEscaped()
	{
		TEST_ASSERT_EQ(QuotedText("hi", true), std::string("\"hi\""));
		TEST_ASSERT_EQ(QuotedText("a\"b\\c", true), std::string("\"a\\\"b\\\\c\""));
		TEST_ASSERT_EQ(QuotedText("line\n\ttab\x01", true), std::string("\"line\\n\\ttab\\x01\""));
		TEST_ASSERT_EQ(QuotedText("", true), std::string("\"\""));
		TEST_ASSERT_EQ(QuotedText("long", false), std::string("\"long\"..."));
		TEST_PASSED();
	}

	void DapFormattingTest::TypeDescriptorNamesAreScoped()
	{
		TEST_ASSERT_EQ(TypeDescriptorName(".?AVBadCast@@"), std::string("BadCast"));
		TEST_ASSERT_EQ(TypeDescriptorName(".?AUPoint@Geo@@"), std::string("Geo::Point"));
		TEST_ASSERT_EQ(TypeDescriptorName(".?AVInner@Outer@Ns@@"), std::string("Ns::Outer::Inner"));
		TEST_ASSERT_EQ(TypeDescriptorName(".?ATBits@@"), std::string("Bits"));
		TEST_ASSERT_EQ(TypeDescriptorName(".H"), std::string(".H"));                   // a primitive: as it is
		TEST_ASSERT_EQ(TypeDescriptorName(".?AV?$Box@H@@"), std::string(".?AV?$Box@H@@")); // a template: as it is
		TEST_ASSERT_EQ(TypeDescriptorName(".?AVOpen"), std::string(".?AVOpen"));         // unterminated
		TEST_ASSERT_EQ(TypeDescriptorName(""), std::string(""));
		TEST_PASSED();
	}

	void DapFormattingTest::OutputIsCutOnlyBetweenCharacters()
	{
		TEST_ASSERT_EQ(CompleteUtf8Prefix(""), size_t(0));
		TEST_ASSERT_EQ(CompleteUtf8Prefix("plain"), size_t(5));
		TEST_ASSERT_EQ(CompleteUtf8Prefix("e\xC3\xA9"), size_t(3));         // a whole two-byte character
		TEST_ASSERT_EQ(CompleteUtf8Prefix("e\xC3"), size_t(1));             // its first byte waits
		TEST_ASSERT_EQ(CompleteUtf8Prefix("x\xF0\x9F\x98"), size_t(1));     // three of four bytes wait
		TEST_ASSERT_EQ(CompleteUtf8Prefix("x\xF0\x9F\x98\x80"), size_t(5)); // all four
		TEST_ASSERT_EQ(CompleteUtf8Prefix("\x80\x80"), size_t(2));          // stray continuation bytes pass on
		TEST_PASSED();
	}

	void DapFormattingTest::CommandLinesSurviveTheRuntimesSplit()
	{
		const std::vector<std::string> args = { "plain", "two words", "", "quote\"inside", "trailing\\", "back\\\"slash", "tab\there" };
		const std::vector<std::wstring> split = Split(CommandLine("C:\\Program Files\\app.exe", args));
		TEST_ASSERT_EQ(split.size(), args.size() + 1);
		TEST_ASSERT(split[0] == L"C:\\Program Files\\app.exe");
		TEST_ASSERT(split[1] == L"plain");
		TEST_ASSERT(split[2] == L"two words");
		TEST_ASSERT(split[3] == L"");
		TEST_ASSERT(split[4] == L"quote\"inside");
		TEST_ASSERT(split[5] == L"trailing\\");
		TEST_ASSERT(split[6] == L"back\\\"slash");
		TEST_ASSERT(split[7] == L"tab\there");
		TEST_PASSED();
	}
} // namespace Testing
