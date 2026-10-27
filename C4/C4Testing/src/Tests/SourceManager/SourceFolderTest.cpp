#include "SourceFolderTest.h"
#include "SourceManager/SourceManager.h"
#include "Compiler/FileIo.h"
#include <process.h>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// A folder in the temp folder, this process's own, removed with the test; made and removed by its extended
		// spelling, so it may be deeper than MAX_PATH.
		struct Folder
		{
			fs::path root;

			explicit Folder(const char* name)
				: root(fs::temp_directory_path() / (std::string("c4_folder_") + name + "_" + std::to_string(_getpid())))
			{
				std::error_code ec;
				fs::remove_all(ExtendedPath(root), ec);
				fs::create_directories(ExtendedPath(root), ec);
			}
			~Folder() { std::error_code ec; fs::remove_all(ExtendedPath(root), ec); }

			void Write(const fs::path& file, const std::string& text)
			{
				const fs::path path = root / file;
				CreateFolders(PathText(path.parent_path()));
				WriteFileBytes(PathText(path), text);
			}

			std::string Utf8(const fs::path& file) const
			{
				const std::u8string text = (root / file).lexically_normal().u8string();
				std::string out(text.begin(), text.end());
				for (char& c : out) if (c == '\\') c = '/';
				return out;
			}
		};

		const fs::path kData = fs::path(u8"\u6570\u636e.cf");                 // two CJK characters: no ANSI code page holds them
		const fs::path kNotes = fs::path(u8"\u0437\u0430\u043c\u0435\u0442\u043a\u0438.txt");   // Cyrillic, not a source
		const std::string kDataUtf8 = "\xe6\x95\xb0\xe6\x8d\xae.cf";
	}

	void SourceFolderTest::NamesOutsideTheCodePageAreUtf8()
	{
		Folder folder("utf8");
		folder.Write(kData, "int32 x = 1;\n");
		folder.Write(kNotes, "not C4\n");
		folder.Write(fs::path(u8"\u0434\u0438\u0440") / "inner.ch", "int32 y = 2;\n");
		SourceManager sources;
		TEST_ASSERT_EQ(sources.RegisterFolder(folder.root), 2u);
		const uint32 id = sources.FindByAbsPath(folder.Utf8(kData));
		TEST_ASSERT(id != 0);
		TEST_ASSERT_EQ(sources.GetInfo(id).displayName, kDataUtf8);
		TEST_ASSERT(sources.FindByAbsPath(folder.Utf8(fs::path(u8"\u0434\u0438\u0440") / "inner.ch")) != 0);
		TEST_PASSED();
	}

	void SourceFolderTest::OnlyC4SourcesAreRegistered()
	{
		Folder folder("kinds");
		folder.Write("a.cf", "int32 a = 1;\n");
		folder.Write("b.ch", "int32 b = 2;\n");
		folder.Write("c.cpp", "int c = 3;\n");
		folder.Write("d.cf.bak", "int32 d = 4;\n");
		SourceManager sources;
		TEST_ASSERT_EQ(sources.RegisterFolder(folder.root), 2u);
		TEST_ASSERT_EQ(sources.RegisterFolder(folder.root / "missing"), 0u);
		TEST_PASSED();
	}

	// BUG-392: `extern "c" include "x.h"` names a C header for the interop pass; the library's files are only what the
	// program's C4 includes name. A header found under the library root (an absolute path always is) was parsed as C4.
	void SourceFolderTest::AForeignIncludeIsNoLibraryFile()
	{
		Folder program("foreign_program");
		Folder library("foreign_library");
		library.Write("native.h", "int answer(void);\n");
		library.Write("other.h", "int other(void);\n");
		library.Write("lib.ch", "int32 lib = 1;\n");
		program.Write("main.cf", "extern \"c\" include \"" + library.Utf8("native.h") + "\"\n"
			"extern \"c++\"\tinclude \"other.h\" { define(\"X\"); }\n"
			"include \"lib.ch\"\nint32 main() { return lib; }\n");
		SourceManager sources;
		TEST_ASSERT_EQ(sources.RegisterFolder(program.root), 1u);
		TEST_ASSERT_EQ(sources.RegisterIncludedLibraryFiles(library.root), 1u);
		TEST_ASSERT(sources.FindByAbsPath(library.Utf8("lib.ch")) != 0);
		TEST_ASSERT(sources.FindByAbsPath(library.Utf8("native.h")) == 0);
		TEST_ASSERT(sources.FindByAbsPath(library.Utf8("other.h")) == 0);
		TEST_PASSED();
	}

	void SourceFolderTest::ADeepFolderRegistersAndReads()
	{
		Folder folder("deep");
		const fs::path deep = fs::path(std::string(120, 'd')) / std::string(120, 'e') / "deep.cf";
		const std::string text = "int32 deep = 3;\n";
		folder.Write(deep, text);
		TEST_ASSERT(PathText(folder.root / deep).size() > 260);
		SourceManager sources;
		TEST_ASSERT_EQ(sources.RegisterFolder(folder.root), 1u);
		const uint32 id = sources.FindByAbsPath(folder.Utf8(deep));
		TEST_ASSERT(id != 0);
		TEST_ASSERT_EQ(std::string(sources.GetInfo(id).source->Data()), text);
		SourceManager single;
		const uint32 file = single.RegisterFile(folder.root / deep);
		TEST_ASSERT(file != 0);
		TEST_ASSERT_EQ(std::string(single.GetInfo(file).source->Data()), text);
		TEST_PASSED();
	}
} // namespace Testing
