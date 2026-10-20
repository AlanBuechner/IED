#include "StaticReflectionTest.h"
#include <string>
#include <vector>

namespace Testing
{
	void StaticReflectionTest::TestPropertySerializationRoundTrip()
	{
		// Test that a Reflection::Property can be serialized and deserialized correctly.
		std::vector<std::string> flags = {"readonly"};
		std::vector<Reflect::Attribute> attributes;
		attributes.push_back(Reflect::Attribute("min", "0"));
		attributes.push_back(Reflect::Attribute("max", "100"));

		Reflect::Property prop("testProp", 42, sizeof(uint32), 8, flags, attributes);

		if (prop.GetName() != "testProp")
			TEST_FAILED("Property name mismatch");

		if (prop.GetTypeID() != 42)
			TEST_FAILED("Property typeID mismatch");

		if (prop.GetSize() != sizeof(uint32))
			TEST_FAILED("Property size mismatch");

		if (prop.GetOffset() != 8)
			TEST_FAILED("Property offset mismatch");

		if (!prop.HasFlag("readonly"))
			TEST_FAILED("Property missing readonly flag");

		if (!prop.HasAttribute("min") || prop.GetAttribute("min").GetValue() != "0")
			TEST_FAILED("Property min attribute mismatch");

		if (!prop.HasAttribute("max") || prop.GetAttribute("max").GetValue() != "100")
			TEST_FAILED("Property max attribute mismatch");

		// Test StaticReflection::Property serialization round-trip
		Core::String attribStr = "readonly, min=0, max=100";
		Reflection::Property staticProp("testProp", "uint32", attribStr);

		if (staticProp.m_Name != "testProp")
			TEST_FAILED("Static property name mismatch");

		if (staticProp.m_Type != "uint32")
			TEST_FAILED("Static property type mismatch");

		// Serialize to ObjectDescription
		Core::ObjectDescription desc = staticProp.Save();

		// Verify the description contains expected data
		if (!desc.IsObject())
			TEST_FAILED("Expected object description from Property::Save()");

		// Load back
		Reflection::Property loadedProp = Reflection::Property::Load(desc);

		if (loadedProp.m_Name != "testProp")
			TEST_FAILED("Loaded property name mismatch");

		if (loadedProp.m_Type != "uint32")
			TEST_FAILED("Loaded property type mismatch");

		// Verify attributes are preserved
		bool hasReadonly = false;
		for (const auto& attr : loadedProp.m_Attributes) {
			if (attr.name == "readonly" || attr.name == "min" || attr.name == "max")
				hasReadonly = true;
		}

		if (!hasReadonly)
			TEST_FAILED("Loaded property missing attributes");

		TEST_PASSED();
	}

	void StaticReflectionTest::TestFunctionSerializationRoundTrip()
	{
		// Test that a Reflection::Function can be serialized and deserialized correctly.
		std::vector<std::string> flags = {"Public"};
		std::vector<Reflect::Attribute> attributes;

		Reflect::Function func("TestMethod", nullptr, flags, attributes);

		if (func.GetName() != "TestMethod")
			TEST_FAILED("Function name mismatch");

		if (!func.HasFlag("Public"))
			TEST_FAILED("Function missing Public flag");

		// Test StaticReflection::Function serialization round-trip
		Core::Vector<Core::String> paramTypes;
		paramTypes.Push("int");
		paramTypes.Push("string");
		Core::String attribStr = "Public";
		Reflection::Function staticFunc("TestMethod", paramTypes, attribStr);

		if (staticFunc.m_Name != "TestMethod")
			TEST_FAILED("Static function name mismatch");

		if (staticFunc.m_Types.Count() != 2) {
			Core::String msg = Core::FormatString("Expected 2 parameter types, got {}", staticFunc.m_Types.Count());
			TEST_FAILED(msg);
		}

		// Serialize to ObjectDescription
		Core::ObjectDescription desc = staticFunc.Save();

		if (!desc.IsObject())
			TEST_FAILED("Expected object description from Function::Save()");

		// Load back
		Reflection::Function loadedFunc = Reflection::Function::Load(desc);

		if (loadedFunc.m_Name != "TestMethod")
			TEST_FAILED("Loaded function name mismatch");

		if (loadedFunc.m_Types.Count() != 2) {
			Core::String msg = Core::FormatString("Expected 2 parameter types after load, got {}", loadedFunc.m_Types.Count());
			TEST_FAILED(msg);
		}

		TEST_PASSED();
	}

	void StaticReflectionTest::TestClassSerializationRoundTrip()
	{
		// Test that a Reflection::Class can be serialized and deserialized correctly.
		std::vector<std::string> flags = {"EditorOnly"};
		std::vector<Reflect::Attribute> attributes;
		attributes.push_back(Reflect::Attribute("category", "Testing"));

		std::vector<Reflect::Property> properties;
		properties.push_back(Reflect::Property("x", 42, sizeof(float), 0, {}, {}));
		properties.push_back(Reflect::Property("y", 42, sizeof(float), 4, {}, {}));

		std::vector<Reflect::Function> functions;
		functions.push_back(Reflect::Function("GetValue", nullptr, {}, {}));

		Reflect::Class testClass(
			"TestClass",
			"Testing::TestClass",
			600, // typeID
			sizeof(float) * 2,
			"TestGroup",
			"", // no scripting language ID (static class)
		 nullptr,
		 nullptr,
			flags,
			attributes,
			properties,
			functions
		);

		if (testClass.GetName() != "TestClass")
			TEST_FAILED("Class name mismatch");

		if (testClass.GetSname() != "Testing::TestClass")
			TEST_FAILED("Class semantic name mismatch");

		if (testClass.GetTypeID() != 600)
			TEST_FAILED("Class typeID mismatch");

		if (testClass.GetProperties().size() != 2)
			TEST_FAILED("Expected 2 properties");

		if (testClass.GetFunctions().size() != 1)
			TEST_FAILED("Expected 1 function");

		if (!testClass.HasFlag("EditorOnly"))
			TEST_FAILED("Class missing EditorOnly flag");

		if (!testClass.HasAttribute("category"))
			TEST_FAILED("Class missing category attribute");

		if (testClass.GetAttribute("category").GetValue() != "Testing")
			TEST_FAILED("Class category attribute value mismatch");

		// Test StaticReflection::Class serialization round-trip
		Core::String classAttrib = "EditorOnly, category=Testing";
		Reflection::Class staticClass("Testing::TestClass", classAttrib);

		if (staticClass.m_Name != "TestClass")
			TEST_FAILED("Static class name mismatch");

		// Add properties and functions to the static class
		staticClass.m_Props.Push(Reflection::Property("x", "float", ""));
		staticClass.m_Props.Push(Reflection::Property("y", "float", ""));
		Core::Vector<Core::String> emptyTypes;
		staticClass.m_Functions.Push(Reflection::Function("GetValue", emptyTypes, ""));

		// Serialize to ObjectDescription
		Core::ObjectDescription desc = staticClass.Save();

		if (!desc.IsObject())
			TEST_FAILED("Expected object description from Class::Save()");

		// Load back
		Reflection::Class loadedClass = Reflection::Class::Load(desc);

		if (loadedClass.m_Name != "TestClass")
			TEST_FAILED("Loaded class name mismatch");

		if (loadedClass.m_Props.Count() != 2) {
			Core::String msg = Core::FormatString("Expected 2 properties after load, got {}", loadedClass.m_Props.Count());
			TEST_FAILED(msg);
		}

		if (loadedClass.m_Functions.Count() != 1) {
			Core::String msg = Core::FormatString("Expected 1 function after load, got {}", loadedClass.m_Functions.Count());
			TEST_FAILED(msg);
		}

		TEST_PASSED();
	}

	void StaticReflectionTest::TestStaticClassBackwardCompatibility()
	{
		// Test that static classes (empty scripting language ID) are unaffected by reload operations.
		const std::string scriptingLangID = "compat-test-lang";

		std::vector<std::string> flags;
		std::vector<Reflect::Attribute> attributes;
		std::vector<Reflect::Property> properties;
		std::vector<Reflect::Function> functions;

		// Register a static class (no scripting language ID)
		Reflect::Class staticClass(
			"CompatStaticClass",
			"Testing::CompatStaticClass",
			601, // typeID
			sizeof(int),
			"TestGroup",
			"",  // empty = static
		 nullptr,
		 nullptr,
			flags,
			attributes,
			properties,
			functions
		);
		Reflect::Registry::GetRegistry()->AddClass(staticClass);

		// Register a scripting class
		Reflect::Class scriptingClass(
			"CompatScriptingClass",
			"Testing::CompatScriptingClass",
			602, // typeID
			sizeof(int),
			"TestGroup",
			scriptingLangID,
		 nullptr,
		 nullptr,
			flags,
			attributes,
			properties,
			functions
		);
		Reflect::Registry::GetRegistry()->AddClass(scriptingClass);

		// Both should be accessible initially
		const Reflect::Class* staticBefore = Reflect::Registry::GetRegistry()->GetClass("Testing::CompatStaticClass");
		const Reflect::Class* scriptingBefore = Reflect::Registry::GetRegistry()->GetClass("Testing::CompatScriptingClass");

		if (!staticBefore)
			TEST_FAILED("Static class not found before unload");

		if (!scriptingBefore)
			TEST_FAILED("Scripting class not found before unload");

		// Unload scripting language - static class should survive
		Reflect::Scripting::UnloadScriptingReflection(scriptingLangID);

		// Static class should still be accessible
		const Reflect::Class* staticAfter = Reflect::Registry::GetRegistry()->GetClass("Testing::CompatStaticClass");
		if (!staticAfter)
			TEST_FAILED("Static class removed during scripting language unload - backward compatibility broken");

		// Verify static class data is intact
		if (staticAfter->GetTypeID() != 601)
			TEST_FAILED("Static class typeID changed during unload");

		if (staticAfter->GetGroup() != "TestGroup")
			TEST_FAILED("Static class group changed during unload");

		if (!staticAfter->GetScriptingLanguageID().empty())
			TEST_FAILED("Static class scripting language ID should remain empty");

		// Scripting class should be removed
		const Reflect::Class* scriptingAfter = Reflect::Registry::GetRegistry()->GetClass("Testing::CompatScriptingClass");
		if (scriptingAfter)
			TEST_FAILED("Scripting class should be removed after unload");

		// Verify the group index is also correct - static classes in groups should remain
		const auto& group = Reflect::Registry::GetRegistry()->GetGroup("TestGroup");
		bool staticInGroup = false;
		for (const Reflect::Class* cls : group) {
			if (cls->GetName() == "CompatStaticClass") {
				staticInGroup = true;
				break;
			}
		}

		if (!staticInGroup)
			TEST_FAILED("Static class removed from group index during unload");

		TEST_PASSED();
	}
}
