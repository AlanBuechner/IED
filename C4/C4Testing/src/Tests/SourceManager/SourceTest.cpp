#include "SourceTest.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace Testing
{
	namespace
	{
		struct TempFileGuard
		{
			fs::path path;
			~TempFileGuard() { std::error_code ec; fs::remove(path, ec); }
		};

		struct TempDirGuard
		{
			fs::path path;
			~TempDirGuard() { std::error_code ec; if (!path.empty()) fs::remove_all(path, ec); }
		};

		fs::path MakeTempDir(const char* name)
		{
			fs::path dir = fs::temp_directory_path() / (std::string("c4_srtest_") + name);
			std::error_code ec;
			fs::remove_all(dir, ec);
			fs::create_directories(dir, ec);
			return dir;
		}

		void WriteFile(const fs::path& file, const std::string& content)
		{
			std::error_code ec;
			fs::create_directories(file.parent_path(), ec);
			std::ofstream out(file, std::ios::binary);
			out << content;
		}

		fs::path WriteTempFile(const char* name, const std::string& content)
		{
			fs::path dir = fs::temp_directory_path();
			fs::path file = dir / name;
			std::ofstream out(file, std::ios::binary);
			out << content;
			return file;
		}

		uint32 ToU32(size_t value) { return static_cast<uint32>(value); }

		int CharCode(char c) { return static_cast<int>(c); }
	}

	void SourceTest::TestMemoryEmptySource()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("");
		TEST_ASSERT_EQ(ToU32(src->Size()), 0u);
		TEST_ASSERT_EQ(src->ReadChar(), -1);
		TEST_ASSERT_EQ(src->ReadCharAt(0), -1);
		uint32 line = 0, column = 0;
		src->ByteOffsetToLineColumn(0, line, column);
		TEST_ASSERT_EQ(line, 1u);
		TEST_ASSERT_EQ(column, 1u);
		TEST_PASSED();
	}

	void SourceTest::TestMemorySequentialReadToEOF()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("abc");
		TEST_ASSERT_EQ(ToU32(src->Size()), 3u);
		TEST_ASSERT_EQ(src->ReadChar(), CharCode('a'));
		TEST_ASSERT_EQ(src->ReadChar(), CharCode('b'));
		TEST_ASSERT_EQ(src->ReadChar(), CharCode('c'));
		TEST_ASSERT_EQ(src->ReadChar(), -1);
		TEST_ASSERT_EQ(ToU32(src->GetReadPosition()), 3u);
		TEST_PASSED();
	}

	void SourceTest::TestMemoryPushBack()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("abc");
		src->ReadChar();
		src->ReadChar();
		src->PushBack();
		TEST_ASSERT_EQ(ToU32(src->GetReadPosition()), 1u);
		TEST_ASSERT_EQ(src->ReadChar(), CharCode('b'));
		TEST_PASSED();
	}

	void SourceTest::TestMemoryPushBackAtStartClamps()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("abc");
		src->PushBack();
		TEST_ASSERT_EQ(ToU32(src->GetReadPosition()), 0u);
		src->Seek(1);
		src->PushBack();
		src->PushBack();
		TEST_ASSERT_EQ(ToU32(src->GetReadPosition()), 0u);
		TEST_PASSED();
	}

	void SourceTest::TestMemorySeekThenRead()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("hello");
		src->Seek(4);
		TEST_ASSERT_EQ(src->ReadChar(), CharCode('o'));
		TEST_ASSERT_EQ(src->ReadChar(), -1);
		TEST_PASSED();
	}

	void SourceTest::TestMemorySeekBeyondEndClamps()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("hi");
		src->Seek(999);
		TEST_ASSERT_EQ(ToU32(src->GetReadPosition()), 2u);
		TEST_ASSERT_EQ(src->ReadChar(), -1);
		TEST_PASSED();
	}

	void SourceTest::TestMemoryMultipleSeeks()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("abcdef");
		src->Seek(3);
		src->Seek(1);
		TEST_ASSERT_EQ(src->ReadChar(), CharCode('b'));
		src->Seek(5);
		TEST_ASSERT_EQ(src->ReadChar(), CharCode('f'));
		TEST_PASSED();
	}

	void SourceTest::TestMemoryReadCharAtOutOfRange()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("xy");
		TEST_ASSERT_EQ(src->ReadCharAt(0), CharCode('x'));
		TEST_ASSERT_EQ(src->ReadCharAt(1), CharCode('y'));
		TEST_ASSERT_EQ(src->ReadCharAt(2), -1);
		TEST_ASSERT_EQ(src->ReadCharAt(100), -1);
		TEST_PASSED();
	}

	void SourceTest::TestMemoryHighByteValues()
	{
		std::string content;
		content.push_back(static_cast<char>(0xFF));
		content.push_back(static_cast<char>(0x80));
		content.push_back('\0');
		Ref<Scripting::Source> src = Scripting::Source::FromMemory(content);
		TEST_ASSERT_EQ(src->ReadCharAt(0), 255);
		TEST_ASSERT_EQ(src->ReadCharAt(1), 128);
		TEST_ASSERT_EQ(src->ReadCharAt(2), 0);
		TEST_PASSED();
	}

	void SourceTest::TestMemoryStripsUtf8Bom()
	{
		// EF BB BF + "abc" -> the BOM must be stripped, leaving exactly "abc".
		std::string content;
		content.push_back(static_cast<char>(0xEF));
		content.push_back(static_cast<char>(0xBB));
		content.push_back(static_cast<char>(0xBF));
		content += "abc";
		Ref<Scripting::Source> src = Scripting::Source::FromMemory(content);
		TEST_ASSERT_EQ(ToU32(src->Size()), 3u);
		TEST_ASSERT_EQ(src->ReadCharAt(0), CharCode('a'));
		TEST_ASSERT_EQ(src->ReadCharAt(1), CharCode('b'));
		TEST_ASSERT_EQ(src->ReadCharAt(2), CharCode('c'));
		TEST_ASSERT_EQ(src->ReadCharAt(3), -1);

		// A BOM that is only 2 of the 3 bytes must NOT be stripped (it is content, not a BOM).
		std::string partial;
		partial.push_back(static_cast<char>(0xEF));
		partial.push_back(static_cast<char>(0xBB)); // missing the third byte 0xBF
		partial += "a";
		Ref<Scripting::Source> src2 = Scripting::Source::FromMemory(partial);
		TEST_ASSERT_EQ(ToU32(src2->Size()), 3u); // nothing removed

		// A lone 0xEF byte is content, not a BOM.
		std::string lone;
		lone.push_back(static_cast<char>(0xEF));
		lone += "a";
		Ref<Scripting::Source> src3 = Scripting::Source::FromMemory(lone);
		TEST_ASSERT_EQ(ToU32(src3->Size()), 2u);
		TEST_PASSED();
	}

	void SourceTest::TestFileStripsUtf8Bom()
	{
		// A file saved with a BOM (the Visual Studio / Notepad default) must read back without one.
		TempFileGuard guard;
		guard.path = WriteTempFile("c4_srtest_bom.cf", "\xEF\xBB\xBFint x;\n");
		Ref<Scripting::Source> src = Ref<Scripting::Source>(new Scripting::Source(guard.path));
		TEST_ASSERT_EQ(ToU32(src->Size()), 7u); // "int x;\n" - BOM removed
		TEST_ASSERT_EQ(src->ReadCharAt(0), CharCode('i'));
		uint32 line = 0, column = 0;
		src->ByteOffsetToLineColumn(0, line, column);
		TEST_ASSERT_EQ(line, 1u); // line table rebuilt after BOM strip
		TEST_PASSED();
	}

	void SourceTest::TestLineToByteOffsetMultiLine()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("abc\ndef\nghi");
		TEST_ASSERT_EQ(ToU32(src->LineToByteOffset(1)), 0u);
		TEST_ASSERT_EQ(ToU32(src->LineToByteOffset(2)), 4u);
		TEST_ASSERT_EQ(ToU32(src->LineToByteOffset(3)), 8u);
		TEST_PASSED();
	}

	void SourceTest::TestLineToByteOffsetOutOfRange()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("a\nb");
		TEST_ASSERT_EQ(ToU32(src->LineToByteOffset(0)), 0u);
		TEST_ASSERT_EQ(ToU32(src->LineToByteOffset(3)), 3u);
		TEST_ASSERT_EQ(ToU32(src->LineToByteOffset(100)), 3u);
		TEST_PASSED();
	}

	void SourceTest::TestGetLineTextExcludesNewline()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("one\ntwo\nthree\n");
		TEST_ASSERT(src->GetLineText(1) == "one");
		TEST_ASSERT(src->GetLineText(2) == "two");
		TEST_ASSERT(src->GetLineText(3) == "three");
		TEST_PASSED();
	}

	void SourceTest::TestGetLineTextLastLineNoNewline()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("x\ny");
		TEST_ASSERT(src->GetLineText(2) == "y");
		TEST_PASSED();
	}

	void SourceTest::TestGetLineTextInvalidLine()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("a\nb");
		TEST_ASSERT(src->GetLineText(0).empty());
		TEST_ASSERT(src->GetLineText(3).empty());
		TEST_PASSED();
	}

	void SourceTest::TestByteOffsetToLineColumn()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("abc\ndef\nghi");
		uint32 line = 0, column = 0;
		src->ByteOffsetToLineColumn(0, line, column);
		TEST_ASSERT_EQ(line, 1u);
		TEST_ASSERT_EQ(column, 1u);
		src->ByteOffsetToLineColumn(4, line, column);
		TEST_ASSERT_EQ(line, 2u);
		TEST_ASSERT_EQ(column, 1u);
		src->ByteOffsetToLineColumn(5, line, column);
		TEST_ASSERT_EQ(line, 2u);
		TEST_ASSERT_EQ(column, 2u);
		src->ByteOffsetToLineColumn(9, line, column);
		TEST_ASSERT_EQ(line, 3u);
		TEST_ASSERT_EQ(column, 2u);
		TEST_PASSED();
	}

	void SourceTest::TestByteOffsetToLineColumnEmptySource()
	{
		Ref<Scripting::Source> src = Scripting::Source::FromMemory("");
		uint32 line = 99, column = 99;
		src->ByteOffsetToLineColumn(0, line, column);
		TEST_ASSERT_EQ(line, 1u);
		TEST_ASSERT_EQ(column, 1u);
		TEST_PASSED();
	}

	void SourceTest::TestFileReadsDiskContent()
	{
		TempFileGuard guard;
		const std::string content = "line one\nline two\n";
		guard.path = WriteTempFile("c4_source_test_file.cf", content);

		Scripting::Source src(guard.path);
		TEST_ASSERT_EQ(ToU32(src.Size()), ToU32(content.size()));
		std::string read;
		int c;
		while ((c = src.ReadChar()) != -1)
			read.push_back(static_cast<char>(c));
		TEST_ASSERT(read == content);

		uint32 line = 0, column = 0;
		src.ByteOffsetToLineColumn(9, line, column);
		TEST_ASSERT_EQ(line, 2u);
		TEST_ASSERT_EQ(column, 1u);
		TEST_PASSED();
	}

	void SourceTest::TestFileMissingPathIsEmpty()
	{
		Scripting::Source src(fs::temp_directory_path() / "c4_source_test_missing_zz.cf");
		TEST_ASSERT_EQ(ToU32(src.Size()), 0u);
		TEST_ASSERT_EQ(src.ReadChar(), -1);
		TEST_PASSED();
	}

	void SourceTest::TestRegisterInMemory()
	{
		Scripting::SourceManager manager;
		uint32 id = manager.RegisterInMemory("test.cf", "int x = 1;\n");
		TEST_ASSERT(id != 0);
		TEST_ASSERT(manager.HasInfo(id));
		TEST_ASSERT_EQ(manager.Count(), 1u);

		const Scripting::SourceInfo& info = manager.GetInfo(id);
		TEST_ASSERT(info.displayName == "test.cf");
		TEST_ASSERT(info.source->GetLineText(1) == "int x = 1;");
		TEST_PASSED();
	}

	void SourceTest::TestRegisterInMemoryDuplicateName()
	{
		Scripting::SourceManager manager;
		uint32 first = manager.RegisterInMemory("dup.cf", "one\n");
		uint32 second = manager.RegisterInMemory("dup.cf", "two\n");
		TEST_ASSERT(first != 0);
		TEST_ASSERT_EQ(second, 0u);
		TEST_ASSERT_EQ(manager.Count(), 1u);
		TEST_PASSED();
	}

	void SourceTest::TestRegisterFileNotFound()
	{
		Scripting::SourceManager manager;
		uint32 id = manager.RegisterFile(fs::temp_directory_path() / "c4_source_test_missing_zz.cf");
		TEST_ASSERT_EQ(id, 0u);
		TEST_ASSERT_EQ(manager.Count(), 0u);
		TEST_PASSED();
	}

	void SourceTest::TestSetSourceTextRebuildsLineTable()
	{
		Scripting::SourceManager manager;
		uint32 id = manager.RegisterInMemory("edit.cf", "a\nb\nc\n");
		manager.SetSourceText(id, "hello\nworld\n");

		const Scripting::SourceInfo& info = manager.GetInfo(id);
		TEST_ASSERT_EQ(ToU32(info.source->Size()), 12u);
		TEST_ASSERT(info.source->GetLineText(1) == "hello");
		TEST_ASSERT(info.source->GetLineText(2) == "world");

		manager.SetSourceText(999, "unknown id must be a no-op\n");
		TEST_PASSED();
	}

	void SourceTest::TestGetLineTextThroughManager()
	{
		Scripting::SourceManager manager;
		uint32 id = manager.RegisterInMemory("lines.cf", "first\nsecond\nthird");
		TEST_ASSERT(manager.GetLineText(id, 1) == "first");
		TEST_ASSERT(manager.GetLineText(id, 3) == "third");
		TEST_ASSERT(manager.GetLineText(id, 0).empty());
		TEST_ASSERT(manager.GetLineText(id, 4).empty());
		TEST_ASSERT(manager.GetLineText(999, 1).empty());
		TEST_PASSED();
	}

	void SourceTest::TestResolveIncludeExactName()
	{
		TempDirGuard guard;
		guard.path = MakeTempDir("exact");
		WriteFile(guard.path / "math.ch", "// math\n");
		WriteFile(guard.path / "cmath.ch", "// cmath\n");

		Scripting::SourceManager manager;
		uint32 mathId = manager.RegisterFile(guard.path / "math.ch");
		uint32 cmathId = manager.RegisterFile(guard.path / "cmath.ch");
		TEST_ASSERT(mathId != 0);
		TEST_ASSERT(cmathId != 0);

		// Exact registered names resolve to themselves.
		TEST_ASSERT_EQ(manager.ResolveIncludePath("math.ch", 0), mathId);
		TEST_ASSERT_EQ(manager.ResolveIncludePath("cmath.ch", 0), cmathId);
		// A name that was never registered must not resolve (no substring leakage).
		TEST_ASSERT_EQ(manager.ResolveIncludePath("nomatch.ch", 0), 0u);
		TEST_PASSED();
	}

	void SourceTest::TestResolveIncludeSubstringDoesNotLeak()
	{
		TempDirGuard guard;
		guard.path = MakeTempDir("substr");
		WriteFile(guard.path / "cmath.ch", "// cmath\n");

		Scripting::SourceManager manager;
		uint32 cmathId = manager.RegisterFile(guard.path / "cmath.ch");
		TEST_ASSERT(cmathId != 0);

		// "math.ch" is a substring of the registered "cmath.ch" display name. A correct
		// resolver must NOT resolve it to cmath.ch - that would silently use the wrong file.
		TEST_ASSERT_EQ(manager.ResolveIncludePath("math.ch", 0), 0u);
		TEST_PASSED();
	}

	void SourceTest::TestResolveIncludeRelativeFromRequester()
	{
		TempDirGuard guard;
		guard.path = MakeTempDir("rel");
		WriteFile(guard.path / "a/main.cf", "// main\n");
		WriteFile(guard.path / "b/util.ch", "// util\n");

		Scripting::SourceManager manager;
		uint32 count = manager.RegisterFolder(guard.path);
		TEST_ASSERT(count >= 2u);

		uint32 mainId = manager.FindSourceId("a/main.cf");
		uint32 utilId = manager.FindSourceId("b/util.ch");
		TEST_ASSERT(mainId != 0);
		TEST_ASSERT(utilId != 0);

		// "../b/util.ch" relative to the requester (a/main.cf) normalizes to "b/util.ch".
		TEST_ASSERT_EQ(manager.ResolveIncludePath("../b/util.ch", mainId), utilId);
		// A plain relative path from a top-level requester has no directory component, so it is
		// not resolved relative to anything; it still matches via the include root.
		TEST_ASSERT_EQ(manager.ResolveIncludePath("b/util.ch", mainId), utilId);
		TEST_PASSED();
	}

	void SourceTest::TestFolderFilesKeepTheirOrderAndText()
	{
		TempDirGuard guard;
		guard.path = MakeTempDir("order");
		for (int i = 0; i < 40; ++i)
			WriteFile(guard.path / (i % 2 ? "nested" : "") / ("file" + std::to_string(i) + ".cf"), "// file " + std::to_string(i) + "\n");

		Scripting::SourceManager manager;
		TEST_ASSERT_EQ(manager.RegisterFolder(guard.path), 40u);
		uint32 previous = 0;
		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(guard.path))
		{
			if (!entry.is_regular_file()) continue;
			const std::string relative = entry.path().lexically_relative(guard.path).generic_string();
			const uint32 id = manager.FindSourceId(relative.c_str());
			TEST_ASSERT(id > previous);
			previous = id;
			const std::string stem = entry.path().stem().string();
			const std::string expected = "// file " + stem.substr(4) + "\n";
			TEST_ASSERT(std::string(manager.GetInfo(id).source->Data()) == expected);
		}
		TEST_PASSED();
	}

	void SourceTest::TestResolveIncludeHasNoBasenameFallback()
	{
		TempDirGuard guard;
		guard.path = MakeTempDir("base");
		WriteFile(guard.path / "math/math.ch", "// math\n");
		WriteFile(guard.path / "other/math.ch", "// other math\n");

		Scripting::SourceManager manager;
		uint32 count = manager.RegisterFolder(guard.path);
		TEST_ASSERT(count >= 2u);

		// A bare "math.ch" names a file beside the includer or at a root, as in C++: it does not pick one of the
		// nested files that happen to share the name (docs/language/15-projects.md, rule 3).
		TEST_ASSERT_EQ(manager.ResolveIncludePath("math.ch", 0), 0u);
		TEST_ASSERT_EQ(manager.ResolveIncludePath("math/math.ch", 0), manager.FindSourceId("math/math.ch"));
		TEST_ASSERT_EQ(manager.ResolveIncludePath("other/math.ch", 0), manager.FindSourceId("other/math.ch"));
		TEST_PASSED();
	}
} // namespace Testing
