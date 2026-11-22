#include "TemplateNamesTest.h"
#include "Compiler/CompilerInstance.h"
#include "Script/TemplateNames.h"
#include <algorithm>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		TemplateNames NamesOf(const std::string& text)
		{
			CompilerInstance compiler;
			const uint32 id = compiler.RegisterInMemory("file.cf", text);
			compiler.CompileAll();
			return TemplateNamesOf(compiler.GetCompilationUnit(id)->GetOriginalChildren());
		}

		bool Holds(const std::vector<std::string>& names, const std::string& name) { return std::find(names.begin(), names.end(), name) != names.end(); }
	}

	void TemplateNamesTest::TemplatesAreDeclaredByTheirQualifiedNames()
	{
		const TemplateNames names = NamesOf(
			"namespace Store {\n"
			"	template<T> class Box { public T value; }\n"
			"	template<T> T Largest(T a, T b) { return a > b ? a : b; }\n"
			"	template<T> alias Ptr = T*;\n"
			"	struct Plain { public int32 k; template<U> public U Scale(U u) { return u; } }\n"
			"	struct Outer { template<T> struct Inner { public T t; } }\n"
			"}\n"
			"namespace A::B { template<T> struct Deep { public T t; } }\n");
		TEST_ASSERT(Holds(names.declared, "Store::Box"));
		TEST_ASSERT(Holds(names.declared, "Store::Largest"));
		TEST_ASSERT(Holds(names.declared, "Store::Ptr"));
		TEST_ASSERT(Holds(names.declared, "Store::Plain")); // its member template is defined out of line by its name
		TEST_ASSERT(Holds(names.declared, "Store::Outer::Inner"));
		TEST_ASSERT(Holds(names.declared, "A::B::Deep"));
		TEST_ASSERT(!Holds(names.declared, "Store::Outer"));
		TEST_ASSERT(names.extended.empty());
		TEST_PASSED();
	}

	void TemplateNamesTest::AdditionsAreWrittenFromTheirNamespace()
	{
		const TemplateNames names = NamesOf(
			"namespace Store { template<T> class Box { public T value; public T Get(); } struct Plain { template<U> public U Offset(U u); } }\n"
			"namespace Store { template<T> T Box<T>::Get() { return value; } }\n"
			"template<V> V Store::Plain::Offset(V v) { return v; }\n"
			"namespace Core { template<K> struct KeyTraits { public static int32 Size() { return 0; } } }\n"
			"namespace Core { template<> struct KeyTraits<float> { public static int32 Size() { return 4; } } }\n"
			"namespace Store { template<T> T Twice() extends Box<T> { return value; } }\n");
		TEST_ASSERT(Holds(names.extended, "Store|Box"));            // the out-of-line member, and the extension's receiver
		TEST_ASSERT(Holds(names.extended, "|Store::Plain"));        // written qualified at file scope
		TEST_ASSERT(Holds(names.extended, "Core|KeyTraits"));       // a specialization
		TEST_ASSERT(Holds(names.declared, "Store::Twice"));         // an extension template is a template too
		TEST_ASSERT_EQ(names.extended.size(), (size_t)3);
		TEST_PASSED();
	}

	void TemplateNamesTest::AnAdditionNamesItsTemplateOutward()
	{
		TEST_ASSERT(TemplateAdditionTargets("Game::Ui|Core::KeyTraits") ==
			(std::vector<std::string>{ "Game::Ui::Core::KeyTraits", "Game::Core::KeyTraits", "Core::KeyTraits" }));
		TEST_ASSERT(TemplateAdditionTargets("|Box") == std::vector<std::string>{ "Box" });
		TEST_ASSERT(TemplateAdditionTargets("no separator").empty());
		TEST_PASSED();
	}

	void TemplateNamesTest::PlainDeclarationsDeclareNoTemplate()
	{
		const TemplateNames names = NamesOf(
			"namespace Store { struct Crate { public int32 Count() { return 1; } } int32 Total(); enum class Kind { A, B } }\n"
			"int32 Store::Total() { return 2; }\n");
		TEST_ASSERT(names.declared.empty() && names.extended.empty());
		TEST_PASSED();
	}

	// A member function and a free function whose symbols agree (BUG-153): the later one's `_1`, and both units kept.
	void TemplateNamesTest::CollidedSymbolsKeepTheirUnits()
	{
		CompilerInstance compiler;
		const uint32 first = compiler.RegisterInMemory("box.cf", "namespace DsA { struct Box { public int32 Get() { return 1; } } }\n");
		const uint32 second = compiler.RegisterInMemory("free.cf", "namespace DsA { int32 Box_Get() { return 3; } }\n");
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		const auto& collided = compiler.GetProgramModel().Symbols()->CollidedSymbols();
		TEST_ASSERT_EQ(collided.size(), (size_t)1);
		const std::vector<uint32>& units = collided.begin()->second;
		TEST_ASSERT_EQ(units.size(), (size_t)2);
		TEST_ASSERT(std::find(units.begin(), units.end(), first) != units.end() && std::find(units.begin(), units.end(), second) != units.end());
		TEST_PASSED();
	}
} // namespace Testing
