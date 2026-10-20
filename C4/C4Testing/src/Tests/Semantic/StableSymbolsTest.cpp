#include "StableSymbolsTest.h"
#include "Backend/CodeGen/CodeGenDriver.h"
#include "Compiler/CompilerInstance.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		using Sources = std::vector<std::pair<const char*, const char*>>;

		// The names of every function and global the program's module defines, or {"<failed>"} when it does not lower.
		std::set<std::string> DefinedSymbols(const Sources& sources)
		{
			CompilerInstance compiler;
			for (const auto& [name, text] : sources) compiler.RegisterInMemory(name, text);
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			llvm::LLVMContext context;
			CodeGenDriver driver(context);
			if (!driver.Lower(compiler.GetProgramModelForLowering(), "main", nullptr)) return { "<failed>" };
			std::set<std::string> names;
			for (const llvm::Function& f : *driver.module())
				if (!f.isDeclaration()) names.insert(f.getName().str());
			for (const llvm::GlobalVariable& g : driver.module()->globals())
				if (!g.isDeclaration()) names.insert(g.getName().str());
			return names;
		}

		std::set<std::string> DefinedSymbols(const char* source) { return DefinedSymbols(Sources{ { "t.cf", source } }); }

		// The symbols of `all` that start with `prefix`.
		std::set<std::string> Starting(const std::set<std::string>& all, const std::string& prefix)
		{
			std::set<std::string> out;
			for (const std::string& s : all)
				if (s.compare(0, prefix.size(), prefix) == 0) out.insert(s);
			return out;
		}

		const char* kTemplates =
			"template<T> class Box { public T v; public T Get() { return v; } }\n"
			"template<T> T Twice(T x) { return x + x; }\n";

		std::string IntThenFloat()
		{
			return std::string(kTemplates) +
				"int32 main() { Box<int32> b; b.v = 2; Box<float> f; f.v = 1.0f; return b.Get() + (int32)f.Get() + Twice<int32>(1) + (int32)Twice<float>(2.0f); }\n";
		}

		std::string FloatThenInt()
		{
			return std::string(kTemplates) +
				"int32 main() { int32 t = (int32)Twice<float>(2.0f) + Twice<int32>(1); Box<float> f; f.v = 1.0f; Box<int32> b; b.v = 2; return t + (int32)f.Get() + b.Get(); }\n";
		}
	}

	void StableSymbolsTest::InstanceSymbolsFollowTheirArguments()
	{
		const std::set<std::string> symbols = DefinedSymbols(IntThenFloat().c_str());
		TEST_ASSERT(symbols.count("Box_Tint32_E_Get_0") == 1);
		TEST_ASSERT(symbols.count("Box_Tfloat_E_Get_0") == 1);
		TEST_ASSERT(symbols.count("Twice_Tint32_E_1int32") == 1);
		TEST_ASSERT(symbols.count("Twice_Tfloat_E_1float") == 1);
		TEST_PASSED();
	}

	void StableSymbolsTest::InstanceSymbolsDoNotDependOnOrder()
	{
		const std::set<std::string> first = DefinedSymbols(IntThenFloat().c_str());
		const std::set<std::string> second = DefinedSymbols(FloatThenInt().c_str());
		TEST_ASSERT(first.count("<failed>") == 0);
		TEST_ASSERT(!Starting(first, "Box_T").empty());
		TEST_ASSERT(Starting(first, "Box_T") == Starting(second, "Box_T"));
		TEST_ASSERT(Starting(first, "Twice_T") == Starting(second, "Twice_T"));
		TEST_PASSED();
	}

	void StableSymbolsTest::QualifiedArgumentsAreEscaped()
	{
		const std::set<std::string> symbols = DefinedSymbols(
			"namespace Geo { struct Point { public int32 x; } }\n"
			"template<T> class Box { public T v; public T Get() { return v; } }\n"
			"int32 main() { Box<Geo::Point> b; b.v.x = 3; Box<Geo::Point*> p; p.v = &b.v; return b.Get().x + p.Get()->x - 6; }\n");
		TEST_ASSERT(symbols.count("Box_TGeo_3a_3aPoint_E_Get_0") == 1);
		TEST_ASSERT(symbols.count("Box_TGeo_3a_3aPoint_2a_E_Get_0") == 1);
		TEST_PASSED();
	}

	void StableSymbolsTest::LambdaSymbolsFollowTheirFunction()
	{
		const char* withOther =
			"int32 Apply(funcptr<int32(int32)> f, int32 v) { return f(v); }\n"
			"int32 F() { return Apply([](int32 x) -> int32 { return x * 3; }, 1); }\n"
			"int32 G() { return Apply([](int32 x) -> int32 { return x + 1; }, 1) + Apply([](int32 x) -> int32 { return x + 2; }, 1); }\n"
			"int32 main() { return F() + G() - 8; }\n";
		const char* alone =
			"int32 Apply(funcptr<int32(int32)> f, int32 v) { return f(v); }\n"
			"int32 G() { return Apply([](int32 x) -> int32 { return x + 1; }, 1) + Apply([](int32 x) -> int32 { return x + 2; }, 1); }\n"
			"int32 main() { return G() - 5; }\n";
		const std::set<std::string> first = DefinedSymbols(withOther);
		const std::set<std::string> second = DefinedSymbols(alone);
		const std::set<std::string> lambdasOfG = Starting(first, "__c4_closure_G_0_");
		TEST_ASSERT(lambdasOfG.size() == 2);
		TEST_ASSERT(lambdasOfG == Starting(second, "__c4_closure_G_0_"));
		TEST_ASSERT(Starting(first, "__c4_closure_F_0_").size() == 1);
		TEST_PASSED();
	}

	void StableSymbolsTest::LifecycleSymbolsFollowTheirType()
	{
		// A heap object whose member has a delete operator gets a compiler-written finalizer.
		const char* resource =
			"class Res { public int32 id; [operator(\"delete\")] public void Drop() { id = 0; } }\n"
			"template<T> class Holder { public Res r; public T v; }\n";
		const std::string intFirst = std::string(resource) +
			"int32 main() { Holder<int32>* a = new Holder<int32>(); Holder<float>* c = new Holder<float>(); return a->r.id + c->r.id; }\n";
		const std::string floatFirst = std::string(resource) +
			"int32 main() { Holder<float>* c = new Holder<float>(); Holder<int32>* a = new Holder<int32>(); return a->r.id + c->r.id; }\n";
		const std::set<std::string> first = DefinedSymbols(intFirst.c_str());
		TEST_ASSERT(first.count("__c4_delete_Holder_Tint32_E") == 1);
		TEST_ASSERT(first.count("__c4_delete_Holder_Tfloat_E") == 1);
		TEST_ASSERT(Starting(first, "__c4_delete_") == Starting(DefinedSymbols(floatFirst.c_str()), "__c4_delete_"));
		TEST_PASSED();
	}

	void StableSymbolsTest::FileStaticGlobalsHaveTheirOwnSymbols()
	{
		const std::set<std::string> symbols = DefinedSymbols(Sources{
			{ "a.cf", "static int32 g_count = 1;\nint32 ReadA() { return g_count; }\n" },
			{ "b.cf", "static int32 g_count = 2;\nint32 ReadB() { return g_count; }\n" },
			{ "main.cf", "int32 ReadA();\nint32 ReadB();\nint32 main() { return ReadA() + ReadB() - 3; }\n" } });
		TEST_ASSERT(symbols.count("g_count_u1") == 1);
		TEST_ASSERT(symbols.count("g_count_u2") == 1);
		TEST_PASSED();
	}

	// BUG-235: the instance key printed float arguments with six decimals, so these were one instance.
	void StableSymbolsTest::CloseFloatArgumentsAreDistinctInstances()
	{
		const std::set<std::string> symbols = DefinedSymbols(
			"template<float F> float Scale() { return F; }\n"
			"int32 main() { return Scale<0.0000001f>() < Scale<0.0000002f>() ? 0 : 1; }\n");
		TEST_ASSERT(Starting(symbols, "Scale_T").size() == 2);
		TEST_PASSED();
	}
} // namespace Testing
