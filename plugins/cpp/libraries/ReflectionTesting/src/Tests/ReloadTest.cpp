#include "ReloadTest.h"
#include <string>
#include <vector>
#include <atomic>

namespace Testing
{
	void ReloadTest::TestSerializeBeforeUnload()
	{
		// Test that we can serialize reflection data before unloading.
		// Register a class with a scripting language ID, then serialize its description.
		const std::string langID = "serialize-test-lang";

		std::vector<std::string> flags;
		std::vector<Reflect::Attribute> attributes;
		std::vector<Reflect::Property> properties;
		std::vector<Reflect::Function> functions;

		// Create a class with a property to serialize
		properties.push_back(Reflect::Property("value", 0, sizeof(uint32), 0, flags, attributes));

		Reflect::Class testClass(
			"SerializeTestClass",
			"SerializeTestClass",
			500, // typeID
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

		// Verify the class is registered and has the expected data
		const Reflect::Class* found = Reflect::Registry::GetRegistry()->GetClass("SerializeTestClass");
		if (!found)
			TEST_FAILED("Registered class not found in registry");

		if (found->GetScriptingLanguageID() != langID)
			TEST_FAILED("Class has wrong scripting language ID");

		if (found->GetProperties().empty())
			TEST_FAILED("Expected properties to be populated");

		// Serialize the class data via ObjectDescription
		Core::ObjectDescription desc;
		desc.GetAsDescriptionMap()["name"] = Core::ObjectDescription::CreateFrom(Core::String(found->GetName()));
		desc.GetAsDescriptionMap()["typeID"] = Core::ObjectDescription::CreateFrom(found->GetTypeID());
		desc.GetAsDescriptionMap()["langID"] = Core::ObjectDescription::CreateFrom(Core::String(found->GetScriptingLanguageID()));

		// Verify serialization round-trip
		if (!desc.HasEntery("name"))
			TEST_FAILED("Serialized description missing name entry");

		if (desc["name"].GetAsString() != "SerializeTestClass")
			TEST_FAILED("Serialized name mismatch");

		if (desc["typeID"].GetAsUint() != 500)
			TEST_FAILED("Serialized typeID mismatch");

		if (desc["langID"].GetAsString() != langID)
			TEST_FAILED("Serialized language ID mismatch");

		TEST_PASSED();
	}

	void ReloadTest::TestRestoreAfterRegistration()
	{
		// Test that after unloading and re-registering, the class is restored correctly.
		const std::string langID = "restore-test-lang";
		const std::string className = "RestoreTestClass";

		std::vector<std::string> flags;
		std::vector<Reflect::Attribute> attributes;
		std::vector<Reflect::Property> properties;
		std::vector<Reflect::Function> functions;

		// Register initial class
		Reflect::Class testClass(
			className,
			className,
			501, // typeID
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

		// Verify it's registered
		if (!Reflect::Scripting::HasScriptingLanguage(langID))
			TEST_FAILED("Expected language ID to be present before unload");

		// Unload the scripting language
		Reflect::Scripting::UnloadScriptingReflection(langID);

		// Verify it's been unloaded
		if (Reflect::Scripting::HasScriptingLanguage(langID))
			TEST_FAILED("Expected language ID to be absent after unload");

		// Re-register the class
		Reflect::Class restoredClass(
			className,
			className,
			501, // typeID
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
		Reflect::Registry::GetRegistry()->AddClass(restoredClass);

		// Verify it's restored
		if (!Reflect::Scripting::HasScriptingLanguage(langID))
			TEST_FAILED("Expected language ID to be present after re-registration");

		const auto& classes = Reflect::Scripting::GetClassesByScriptingLanguageID(langID);
		bool found = false;
		for (const Reflect::Class* cls : classes) {
			if (cls->GetName() == className) {
				found = true;
				break;
			}
		}

		if (!found)
			TEST_FAILED("Restored class not found in scripting language index");

		TEST_PASSED();
	}

	void ReloadTest::TestStaticClassesUnaffectedByUnload()
	{
		// Test that unloading a scripting language doesn't affect classes without that language ID.
		const std::string langID = "unload-static-test-lang";

		std::vector<std::string> flags;
		std::vector<Reflect::Attribute> attributes;
		std::vector<Reflect::Property> properties;
		std::vector<Reflect::Function> functions;

		// Register a static class (no scripting language ID)
		Reflect::Class staticClass(
			"StaticTestClass",
			"StaticTestClass",
			502, // typeID
			sizeof(int),
			"TestGroup",
			"",  // empty scripting language ID = static
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
			"ScriptingTestClass",
			"ScriptingTestClass",
			503, // typeID
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
		Reflect::Registry::GetRegistry()->AddClass(scriptingClass);

		// Unload the scripting language
		Reflect::Scripting::UnloadScriptingReflection(langID);

		// Verify static class is still accessible by name
		const Reflect::Class* staticFound = Reflect::Registry::GetRegistry()->GetClass("StaticTestClass");
		if (!staticFound)
			TEST_FAILED("Static class removed during scripting language unload");

		// Verify scripting class is no longer accessible by name
		const Reflect::Class* scriptingFound = Reflect::Registry::GetRegistry()->GetClass("ScriptingTestClass");
		if (scriptingFound)
			TEST_FAILED("Scripting class should be removed after unload");

		// Verify static class still has correct properties
		if (staticFound->GetName() != "StaticTestClass")
			TEST_FAILED("Static class name changed during unload");

		if (!staticFound->GetGroup().empty() && staticFound->GetGroup() != "TestGroup")
			TEST_FAILED("Static class group changed during unload");

		TEST_PASSED();
	}

	void ReloadTest::TestFullReloadCycle()
	{
		// Full reload cycle: register -> serialize -> unload -> restore -> invoke reload callbacks.
		const std::string langID = "full-reload-lang";
		const std::string className = "FullReloadTestClass";

		std::vector<std::string> flags;
		std::vector<Reflect::Attribute> attributes;
		std::vector<Reflect::Property> properties;
		std::vector<Reflect::Function> functions;

		std::atomic<int> reloadCount{0};
		auto reloadHandle = Reflect::Scripting::SubscribeReload([&](const std::string&) {
			reloadCount++;
		});

		std::atomic<int> unloadCount{0};
		auto unloadHandle = Reflect::Scripting::SubscribeUnload([&](const std::string&) {
			unloadCount++;
		});

		// Phase 1: Register
		Reflect::Class testClass(
			className,
			className,
			504, // typeID
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

		if (!Reflect::Scripting::HasScriptingLanguage(langID))
			TEST_FAILED("Phase 1: Class not registered");

		// Phase 2: Serialize (capture class metadata)
		const auto& classes = Reflect::Scripting::GetClassesByScriptingLanguageID(langID);
		uint64 capturedTypeID = 0;
		for (const Reflect::Class* cls : classes) {
			if (cls->GetName() == className) {
				capturedTypeID = cls->GetTypeID();
				break;
			}
		}

		if (capturedTypeID != 504)
			TEST_FAILED("Phase 2: Failed to capture class metadata");

		// Phase 3: Unload
		Reflect::Scripting::UnloadScriptingReflection(langID);

		if (unloadCount != 1) {
			Core::String msg = Core::FormatString("Expected 1 unload callback, got {}", unloadCount.load());
			Reflect::Scripting::Unsubscribe(reloadHandle);
			Reflect::Scripting::Unsubscribe(unloadHandle);
			TEST_FAILED(msg);
		}

		if (Reflect::Scripting::HasScriptingLanguage(langID)) {
			Reflect::Scripting::Unsubscribe(reloadHandle);
			Reflect::Scripting::Unsubscribe(unloadHandle);
			TEST_FAILED("Phase 3: Language ID should be absent after unload");
		}

		// Phase 4: Restore (re-register)
		Reflect::Class restoredClass(
			className,
			className,
			capturedTypeID, // Use captured typeID
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
		Reflect::Registry::GetRegistry()->AddClass(restoredClass);

		if (!Reflect::Scripting::HasScriptingLanguage(langID)) {
			Reflect::Scripting::Unsubscribe(reloadHandle);
			Reflect::Scripting::Unsubscribe(unloadHandle);
			TEST_FAILED("Phase 4: Class not restored");
		}

		// Phase 5: Invoke reload callbacks
		Reflect::Scripting::InvokeReloadCallbacks(langID);

		if (reloadCount != 1) {
			Core::String msg = Core::FormatString("Expected 1 reload callback, got {}", reloadCount.load());
			Reflect::Scripting::Unsubscribe(reloadHandle);
			Reflect::Scripting::Unsubscribe(unloadHandle);
			TEST_FAILED(msg);
		}

		// Clean up callbacks
		Reflect::Scripting::Unsubscribe(reloadHandle);
		Reflect::Scripting::Unsubscribe(unloadHandle);

		TEST_PASSED();
	}
}
