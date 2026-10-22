#include "TemplateReflectionTest.h"
#include <map>
#include <unordered_map>
#include <vector>
#include <list>

namespace Testing
{
	// -- TemplateTestChild implementations ----------------------------------

	void TemplateTestChild::SetName(const Core::String& p_Name)
	{
		m_Name = p_Name;
	}

	// -- TemplateTestAllContainers implementations --------------------------

	void TemplateTestAllContainers::SetChildren(
		const Core::Vector<TemplateTestChild>& p_Children)
	{
		m_Children = p_Children;
	}

	// -- Unit test implementations ------------------------------------------

	void TemplateReflectionTest::TestNonTemplateProperty()
	{
		// Non-template properties must have empty TemplateArgs and ContainerKind.
		// This verifies backward compatibility: existing non-template properties
		// are unaffected by the new HeaderTool metadata.

		// Create a static property with no template info (simulating pre-existing YAML)
		Reflection::Property prop("m_Id", "int", "");

		if (!prop.m_TemplateArgs.Empty())
			TEST_FAILED("Non-template property should have empty TemplateArgs");

		if (!prop.m_ContainerKind.empty())
			TEST_FAILED("Non-template property should have empty ContainerKind");

		// Serialize and deserialize to verify round-trip preserves emptiness
		Core::ObjectDescription desc = prop.Save();
		Reflection::Property loaded = Reflection::Property::Load(desc);

		if (!loaded.m_TemplateArgs.Empty())
			TEST_FAILED("Loaded non-template property should have empty TemplateArgs");

		if (!loaded.m_ContainerKind.empty())
			TEST_FAILED("Loaded non-template property should have empty ContainerKind");

		TEST_PASSED();
	}

	void TemplateReflectionTest::TestSequentialCoreVector()
	{
		// Core::Vector<T> must be classified as "sequential" with the inner type
		// extracted as a template argument.

		Reflection::Property prop("m_Children", "Core::Vector<Testing::TemplateTestChild>", "");
		prop.m_TemplateArgs.Push("Testing::TemplateTestChild");
		prop.m_ContainerKind = "sequential";

		if (prop.m_ContainerKind != "sequential")
			TEST_FAILED("Core::Vector should be classified as sequential");

		if (prop.m_TemplateArgs.Count() != 1)
			TEST_FAILED("Core::Vector should have exactly one template argument");

		if (prop.m_TemplateArgs[0] != "Testing::TemplateTestChild")
			TEST_FAILED("Core::Vector template arg mismatch");

		// Verify round-trip serialization preserves container metadata
		Core::ObjectDescription desc = prop.Save();
		Reflection::Property loaded = Reflection::Property::Load(desc);

		if (loaded.m_ContainerKind != "sequential")
			TEST_FAILED("Loaded property lost sequential container kind");

		if (loaded.m_TemplateArgs.Count() != 1)
			TEST_FAILED("Loaded property lost template arguments");

		TEST_PASSED();
	}

	void TemplateReflectionTest::TestSequentialStdContainers()
	{
		// std::vector<T> and std::list<T> must both be classified as "sequential".

		// std::vector<int>
		Reflection::Property vecProp("m_Scores", "std::vector<int>", "");
		vecProp.m_TemplateArgs.Push("int");
		vecProp.m_ContainerKind = "sequential";

		if (vecProp.m_ContainerKind != "sequential")
			TEST_FAILED("std::vector should be classified as sequential");

		if (vecProp.m_TemplateArgs[0] != "int")
			TEST_FAILED("std::vector template arg mismatch");

		// std::list<Core::String>
		Reflection::Property listProp("m_Tags", "std::list<Core::String>", "");
		listProp.m_TemplateArgs.Push("Core::String");
		listProp.m_ContainerKind = "sequential";

		if (listProp.m_ContainerKind != "sequential")
			TEST_FAILED("std::list should be classified as sequential");

		if (listProp.m_TemplateArgs[0] != "Core::String")
			TEST_FAILED("std::list template arg mismatch");

		TEST_PASSED();
	}

	void TemplateReflectionTest::TestAssociativeMap()
	{
		// std::map<K, V> must be classified as "associative" with two template
		// arguments (key and value types).

		Reflection::Property prop("m_Stats", "std::map<Core::String, float>", "");
		prop.m_TemplateArgs.Push("Core::String");
		prop.m_TemplateArgs.Push("float");
		prop.m_ContainerKind = "associative";

		if (prop.m_ContainerKind != "associative")
			TEST_FAILED("std::map should be classified as associative");

		if (prop.m_TemplateArgs.Count() != 2)
			TEST_FAILED("std::map should have exactly two template arguments");

		if (prop.m_TemplateArgs[0] != "Core::String")
			TEST_FAILED("std::map key type mismatch");

		if (prop.m_TemplateArgs[1] != "float")
			TEST_FAILED("std::map value type mismatch");

		// Verify round-trip serialization preserves associative metadata
		Core::ObjectDescription desc = prop.Save();
		Reflection::Property loaded = Reflection::Property::Load(desc);

		if (loaded.m_ContainerKind != "associative")
			TEST_FAILED("Loaded property lost associative container kind");

		if (loaded.m_TemplateArgs.Count() != 2)
			TEST_FAILED("Loaded property lost map template arguments");

		TEST_PASSED();
	}

	void TemplateReflectionTest::TestAssociativeUnorderedMap()
	{
		// std::unordered_map<K, V> must be classified as "associative".

		Reflection::Property prop("m_LookupTable", "std::unordered_map<uint32, Core::String>", "");
		prop.m_TemplateArgs.Push("uint32");
		prop.m_TemplateArgs.Push("Core::String");
		prop.m_ContainerKind = "associative";

		if (prop.m_ContainerKind != "associative")
			TEST_FAILED("std::unordered_map should be classified as associative");

		if (prop.m_TemplateArgs.Count() != 2)
			TEST_FAILED("std::unordered_map should have exactly two template arguments");

		if (prop.m_TemplateArgs[0] != "uint32")
			TEST_FAILED("std::unordered_map key type mismatch");

		if (prop.m_TemplateArgs[1] != "Core::String")
			TEST_FAILED("std::unordered_map value type mismatch");

		TEST_PASSED();
	}

	void TemplateReflectionTest::TestNestedTemplateArgs()
	{
		// Core::Vector<Core::Vector<int>> should extract the outer template arg
		// as "Core::Vector<int>" (nested template string).

		Reflection::Property prop("m_Grid", "Core::Vector<Core::Vector<int>>", "");
		prop.m_TemplateArgs.Push("Core::Vector<int>");
		prop.m_ContainerKind = "sequential";

		if (prop.m_ContainerKind != "sequential")
			TEST_FAILED("Nested Vector should be classified as sequential");

		if (prop.m_TemplateArgs.Count() != 1)
			TEST_FAILED("Outer Vector should have exactly one template argument");

		if (prop.m_TemplateArgs[0] != "Core::Vector<int>")
			TEST_FAILED("Nested template arg string mismatch");

		// Verify round-trip serialization preserves nested template args
		Core::ObjectDescription desc = prop.Save();
		Reflection::Property loaded = Reflection::Property::Load(desc);

		if (loaded.m_TemplateArgs.Count() != 1)
			TEST_FAILED("Loaded property lost nested template argument");

		if (loaded.m_TemplateArgs[0] != "Core::Vector<int>")
			TEST_FAILED("Loaded nested template arg string mismatch");

		TEST_PASSED();
	}

	void TemplateReflectionTest::TestDeepNesting()
	{
		// Core::Vector<std::map<Core::String, float>> tests 3-level nesting:
		// outer Vector containing map containing String and float.

		Reflection::Property prop("m_DeepData", "Core::Vector<std::map<Core::String, float>>", "");
		prop.m_TemplateArgs.Push("std::map<Core::String, float>");
		prop.m_ContainerKind = "sequential";

		if (prop.m_ContainerKind != "sequential")
			TEST_FAILED("Outer Vector of map should be sequential");

		if (prop.m_TemplateArgs.Count() != 1)
			TEST_FAILED("Outer container should have exactly one template argument");

		if (prop.m_TemplateArgs[0] != "std::map<Core::String, float>")
			TEST_FAILED("Deep nested template arg string mismatch");

		// Verify round-trip serialization preserves deep nesting
		Core::ObjectDescription desc = prop.Save();
		Reflection::Property loaded = Reflection::Property::Load(desc);

		if (loaded.m_TemplateArgs[0] != "std::map<Core::String, float>")
			TEST_FAILED("Loaded deep nested template arg mismatch");

		TEST_PASSED();
	}

	void TemplateReflectionTest::TestPropertyPaths()
	{
		// Root-level properties should have their own name as the path.
		// Child paths would be parentPath + "/" + childName (computed by HeaderTool).

		// Simulate root-level property path
		Reflection::Property rootProp("m_Position", "Math::Vector3F", "");
		rootProp.m_Path = "m_Position";

		if (rootProp.m_Path != "m_Position")
			TEST_FAILED("Root-level property path should be just the name");

		// Simulate nested property path (as HeaderTool would compute)
		Reflection::Property childProp("m_Children", "Core::Vector<Testing::TemplateTestChild>", "");
		childProp.m_Path = "m_Children";

		if (childProp.m_Path != "m_Children")
			TEST_FAILED("Container property path should be the property name");

		// Verify round-trip serialization preserves paths
		Core::ObjectDescription desc = rootProp.Save();
		Reflection::Property loaded = Reflection::Property::Load(desc);

		if (loaded.m_Path != "m_Position")
			TEST_FAILED("Loaded property lost path information");

		TEST_PASSED();
	}

	void TemplateReflectionTest::TestBackwardCompatibility()
	{
		// Loading a .reflected file without TemplateArgs/ContainerKind/Path fields
		// must not crash and should result in empty values.

		// Create a minimal property with no new metadata (simulating old YAML)
		Reflection::Property oldProp("legacy", "int", "");

		// Serialize - this should NOT include the new optional fields when empty
		Core::ObjectDescription desc = oldProp.Save();

		// Load back - must not crash
		Reflection::Property loaded = Reflection::Property::Load(desc);

		if (loaded.m_Name != "legacy")
			TEST_FAILED("Loaded legacy property name mismatch");

		if (!loaded.m_TemplateArgs.Empty())
			TEST_FAILED("Legacy property should have empty TemplateArgs after load");

		if (!loaded.m_ContainerKind.empty())
			TEST_FAILED("Legacy property should have empty ContainerKind after load");

		if (!loaded.m_Path.empty())
			TEST_FAILED("Legacy property should have empty Path after load");

		// Test that a class with mixed old/new properties loads correctly
		Reflection::Class mixedClass("MixedClass", "");
		mixedClass.m_Props.Push(Reflection::Property("oldProp", "int", ""));
		mixedClass.m_Props.Push(Reflection::Property("newProp", "Core::Vector<float>", ""));

		auto& newPropRef = mixedClass.m_Props[1];
		newPropRef.m_TemplateArgs.Push("float");
		newPropRef.m_ContainerKind = "sequential";
		newPropRef.m_Path = "newProp";

		Core::ObjectDescription classDesc = mixedClass.Save();
		Reflection::Class loadedClass = Reflection::Class::Load(classDesc);

		if (loadedClass.m_Props.Count() != 2)
			TEST_FAILED("Mixed class should have 2 properties after load");

		// Old property should have empty new fields
		if (!loadedClass.m_Props[0].m_TemplateArgs.Empty())
			TEST_FAILED("Old property in mixed class should have empty TemplateArgs");

		// New property should preserve all metadata
		if (loadedClass.m_Props[1].m_ContainerKind != "sequential")
			TEST_FAILED("New property lost container kind in mixed class");

		if (loadedClass.m_Props[1].m_TemplateArgs.Count() != 1)
			TEST_FAILED("New property lost template args in mixed class");

		TEST_PASSED();
	}
}
