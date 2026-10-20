#include "ScriptingIDTest.h"
#include <string>
#include <vector>
#include <algorithm>

namespace Testing
{
	void ScriptingIDTest::TestConsistentHashing()
	{
		// ComputeScriptingLanguageID should produce consistent results for the same paths.
		// We use non-existent paths to avoid filesystem dependencies; the function
		// gracefully handles missing files and still produces a deterministic hash from the path strings.
		std::vector<std::string> paths1 = {"/nonexistent/path1.cs", "/nonexistent/path2.cs"};
		std::vector<std::string> paths2 = {"/nonexistent/path1.cs", "/nonexistent/path2.cs"};

		std::string id1 = Reflect::ComputeScriptingLanguageID(paths1);
		std::string id2 = Reflect::ComputeScriptingLanguageID(paths2);

		if (id1 != id2)
			TEST_FAILED("Expected consistent scripting language ID for same paths");

		// Different paths should produce different IDs
		std::vector<std::string> paths3 = {"/nonexistent/path1.cs", "/nonexistent/path3.cs"};
		std::string id3 = Reflect::ComputeScriptingLanguageID(paths3);

		if (id1 == id3)
			TEST_FAILED("Expected different scripting language IDs for different paths");

		// Empty paths should return empty string
		std::vector<std::string> emptyPaths;
		std::string emptyId = Reflect::ComputeScriptingLanguageID(emptyPaths);
		if (!emptyId.empty())
			TEST_FAILED("Expected empty string for empty path list");

		TEST_PASSED();
	}

	void ScriptingIDTest::TestRegistryFilteringByLanguageID()
	{
		// Register a class with a scripting language ID and verify it appears in the index.
		const std::string langID = "test-lang-1";
		const std::string className = "ScriptingIDTest_TestClass";

		// Create and register a test class with scripting language ID
		std::vector<std::string> flags;
		std::vector<Reflect::Attribute> attributes;
		std::vector<Reflect::Property> properties;
		std::vector<Reflect::Function> functions;

		Reflect::Class testClass(
			className,
			className,  // sname
			0,           // typeID (dummy)
			sizeof(int), // size
			"TestGroup",
			langID,      // scriptingLanguageID
		 nullptr,     // createFunc
		 nullptr,     // destroyFunc
			flags,
			attributes,
			properties,
			functions
		);

		// Register the class
		Reflect::Registry::GetRegistry()->AddClass(testClass);

		// Verify it's in the scripting language index
		const auto& classes = Reflect::Scripting::GetClassesByScriptingLanguageID(langID);
		bool found = false;
		for (const Reflect::Class* cls : classes) {
			if (cls->GetName() == className) {
				found = true;
				break;
			}
		}

		if (!found)
			TEST_FAILED("Registered class not found in scripting language index");

		// Verify HasScriptingLanguage returns true
		if (!Reflect::Scripting::HasScriptingLanguage(langID))
			TEST_FAILED("HasScriptingLanguage returned false for registered language ID");

		// Verify that a non-existent language ID returns empty vector
		const auto& notFound = Reflect::Scripting::GetClassesByScriptingLanguageID("nonexistent-lang");
		if (!notFound.empty())
			TEST_FAILED("Expected empty vector for non-existent language ID");

		TEST_PASSED();
	}

	void ScriptingIDTest::TestHasScriptingLanguage()
	{
		// HasScriptingLanguage should return false for unknown IDs
		if (Reflect::Scripting::HasScriptingLanguage("unknown-lang-id"))
			TEST_FAILED("Expected false for unknown scripting language ID");

		// Should return true for IDs that have registered classes
		// (TestRegistryFilteringByLanguageID already registered a class with "test-lang-1")
		if (!Reflect::Scripting::HasScriptingLanguage("test-lang-1"))
			TEST_FAILED("Expected true for scripting language ID with registered class");

		TEST_PASSED();
	}

	void ScriptingIDTest::TestGetClassesByLanguageID()
	{
		const std::string langID = "test-lang-multi";

		// Register two classes under the same language ID
		std::vector<std::string> flags;
		std::vector<Reflect::Attribute> attributes;
		std::vector<Reflect::Property> properties;
		std::vector<Reflect::Function> functions;

		for (int i = 0; i < 2; i++) {
			std::string name = "MultiLang_Class_" + std::to_string(i);
			Reflect::Class testClass(
				name,
				name,  // sname
				i + 100, // typeID
				sizeof(int),
				"TestGroup",
				langID,
			 nullptr,
			 nullptr,
				flags,
				attributes,
				properties,
				functions
			);
			Reflect::Registry::GetRegistry()->AddClass(testClass);
		}

		const auto& classes = Reflect::Scripting::GetClassesByScriptingLanguageID(langID);

		// Should have at least 2 classes for this language ID
		if (classes.size() < 2) {
			Core::String msg = Core::FormatString("Expected at least 2 classes, got {}", static_cast<uint32>(classes.size()));
			TEST_FAILED(msg);
		}

		// Verify both classes have the correct scripting language ID
		for (const Reflect::Class* cls : classes) {
			if (cls->GetScriptingLanguageID() != langID)
				TEST_FAILED("Class in index has wrong scripting language ID");
		}

		TEST_PASSED();
	}
}
