#include "SourceFolderTest.h"
#include "SourceManager/SourceManager.h"
#include <fstream>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// A folder in the temp folder, removed with the test.
		struct Folder
		{
			fs::path root;

			explicit Folder(const char* name) : root(fs::temp_directory_path() / (std::string("c4_folder_") + name))
			{
				std::error_code ec;
				fs::remove_all(root, ec);
				fs::create_directories(root, ec);
			}
			~Folder() { std::error_code ec; fs::remove_all(root, ec); }

			void Write(const fs::path& file, const std::string& text)
			{
				const fs::path path = root / file;
				fs::create_directories(path.parent_path());
				std::ofstream(path, std::ios::binary) << text;
			}

			std::string Utf8(const fs::path& file) const
			{
				const std::u8string text = (root / file).lexically_normal().u8string();
				std::string out(text.begin(), text.end());
				for (char& c : out) if (c == '\\') c = '/';
				return out;
			}
		};

		const fs::path kData = fs::path(u8"数据.cf");                 // two CJK characters: no ANSI code page holds them
		const fs::path kNotes = fs::path(u8"заметки.txt");   // Cyrillic, not a source
		const std::string kDataUtf8 = "\xe6\x95\xb0\xe6\x8d\xae.cf";
	}

	void SourceFolderTest::NamesOutsideTheCodePageAreUtf8()
	{
		Folder folder("utf8");
		folder.Write(kData, "int32 x = 1;\n");
		folder.Write(kNotes, "not C4\n");
		folder.Write(fs::path(u8"дир") / "inner.ch", "int32 y = 2;\n");
		SourceManager sources;
		TEST_ASSERT_EQ(sources.RegisterFolder(folder.root), 2u);
		const uint32 id = sources.FindByAbsPath(folder.Utf8(kData));
		TEST_ASSERT(id != 0);
		TEST_ASSERT_EQ(sources.GetInfo(id).displayName, kDataUtf8);
		TEST_ASSERT(sources.FindByAbsPath(folder.Utf8(fs::path(u8"дир") / "inner.ch")) != 0);
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
} // namespace Testing
