#include "ReflectionScopeTest.h"
#include "Backend/ProgramBackend.h"
#include "Backend/Reflection/ReflectionScope.h"
#include "Backend/Reflection/RegistrationEmitter.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/Toolchain.h"
#include "Model/ProgramModel.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/QualifiedName.h"
#include <algorithm>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		const char* kProgram =
			"[reflect(\"off\")]\n"
			"struct Hidden { public int32 value; public int32 Twice() const { return value * 2; } public static int32 s_count; }\n"
			"struct Shown { public int32 value; [reflect(\"off\")] public int32 Secret() const { return value; } public int32 Open() const { return value + 1; } }\n"
			"[reflect(\"off\")]\n"
			"namespace Internals\n"
			"{\n"
			"\tstruct Detail { public int32 x; }\n"
			"\tint32 Helper(int32 v) { return v + 1; }\n"
			"\tint32 g_internal = 5;\n"
			"\t[reflect(\"on\")] struct Exposed { public int32 y; }\n"
			"\tnamespace Deeper { struct Inner { public int32 z; } }\n"
			"}\n"
			"namespace Internals { struct Elsewhere { public int32 z; } }\n"
			"[reflect(\"off\")] int32 g_hidden = 7;\n"
			"int32 g_shown = 8;\n"
			"[reflect(\"off\")] template<T> struct Box { public T item; }\n"
			"int32 main() { Box<int32> box; box.item = 1; Hidden h; h.value = 2; Internals::Detail d; d.x = Internals::Helper(1);\n"
			"\treturn box.item + h.Twice() + d.x + g_hidden + Internals::g_internal - 16; }\n";

		struct Built
		{
			CompilerInstance compiler;
			bool ok = false;
		};

		void Build(Built& built, const std::string& text)
		{
			built.compiler.RegisterInMemory("main.cf", text.c_str());
			built.compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			built.compiler.CompileAll();
			built.ok = built.compiler.AnalyzeProgram();
		}

		const ObjectType* TypeNamed(const ProgramModel& model, const char* name)
		{
			for (const ObjectType* type : model.ObjectTypes())
				if (type && QualifiedName(type) == name) return type;
			return nullptr;
		}

		const FunctionRecord* FunctionNamed(const ProgramModel& model, const char* name, const char* owner = nullptr)
		{
			Core::Vector<const FunctionRecord*> records;
			model.AllFunctionRecords(records);
			for (const FunctionRecord* record : records)
				if (record->name == name && (owner ? record->containingType && QualifiedName(record->containingType) == owner : !record->containingType))
					return record;
			return nullptr;
		}

		const SymbolInfo* VariableNamed(const ProgramModel& model, const char* name)
		{
			auto found = model.QuerySymbol(name);
			return found.Ok() ? found.value : nullptr;
		}

		bool Lowered(const std::string& text, std::string& errors, std::vector<std::string>* functions = nullptr)
		{
			Built built;
			Build(built, text);
			if (!built.ok) return false;
			llvm::LLVMContext context;
			ProgramBackend driver(context);
			std::string error;
			const bool lowered = driver.Lower(built.compiler.GetProgramModelForLowering(), "main", &error);
			for (const CompilationError& e : driver.LoweringErrors()) errors += std::string(e.message.c_str()) + "\n";
			if (functions && lowered)
				for (const llvm::Function& f : *driver.module()) functions->push_back(f.getName().str());
			return lowered;
		}

		bool AnyStartsWithAndHolds(const std::vector<std::string>& names, const char* prefix, const char* part)
		{
			return std::any_of(names.begin(), names.end(), [&](const std::string& n) { return n.rfind(prefix, 0) == 0 && n.find(part) != std::string::npos; });
		}
	}

	void ReflectionScopeTest::EverythingIsInByDefault()
	{
		Built built;
		Build(built, "struct Plain { public int32 a; }\nint32 Free() { return 1; }\nint32 g_plain = 2;\nint32 main() { return Free() - 1; }\n");
		TEST_ASSERT(built.ok);
		const ProgramModel& model = built.compiler.GetProgramModel();
		const ReflectionScope scope(model);
		TEST_ASSERT(TypeNamed(model, "Plain") && scope.Has(*TypeNamed(model, "Plain")));
		TEST_ASSERT(FunctionNamed(model, "Free") && scope.Has(*FunctionNamed(model, "Free")));
		TEST_ASSERT(VariableNamed(model, "g_plain") && scope.Has(*VariableNamed(model, "g_plain")));
		TEST_ASSERT(scope.WhyOut(*TypeNamed(model, "Plain")).empty());
		TEST_ASSERT(!scope.Errors().HasErrors());
	}

	void ReflectionScopeTest::TheNearestSettingDecides()
	{
		Built built;
		Build(built, kProgram);
		TEST_ASSERT(built.ok);
		const ProgramModel& model = built.compiler.GetProgramModel();
		const ReflectionScope scope(model);
		TEST_ASSERT(!scope.Has(*TypeNamed(model, "Hidden")));
		TEST_ASSERT(scope.WhyOut(*TypeNamed(model, "Hidden")).find("its [reflect(\"off\")] at main.cf (1:2)") != std::string::npos);
		TEST_ASSERT(!scope.Has(*FunctionNamed(model, "Twice", "Hidden")));          // a member goes with its type
		TEST_ASSERT(!scope.Has(*VariableNamed(model, "Hidden::s_count")));
		TEST_ASSERT(scope.Has(*TypeNamed(model, "Shown")));
		TEST_ASSERT(!scope.Has(*FunctionNamed(model, "Secret", "Shown")));          // its own setting
		TEST_ASSERT(scope.Has(*FunctionNamed(model, "Open", "Shown")));
		TEST_ASSERT(scope.Has(*TypeNamed(model, "Internals::Exposed")));            // nearer than its block
		TEST_ASSERT(!scope.Has(*VariableNamed(model, "g_hidden")));
		TEST_ASSERT(scope.Has(*VariableNamed(model, "g_shown")));
		TEST_ASSERT(TypeNamed(model, "Box<int32>") && !scope.Has(*TypeNamed(model, "Box<int32>"))); // an instance goes with its template
	}

	void ReflectionScopeTest::ANamespaceBlockCoversOnlyItself()
	{
		Built built;
		Build(built, kProgram);
		TEST_ASSERT(built.ok);
		const ProgramModel& model = built.compiler.GetProgramModel();
		const ReflectionScope scope(model);
		TEST_ASSERT(!scope.Has(*TypeNamed(model, "Internals::Detail")));
		TEST_ASSERT(!scope.Has(*TypeNamed(model, "Internals::Deeper::Inner"))); // a block inside it
		TEST_ASSERT(!scope.Has(*FunctionNamed(model, "Helper")));
		TEST_ASSERT(!scope.Has(*VariableNamed(model, "Internals::g_internal")));
		TEST_ASSERT(scope.Has(*TypeNamed(model, "Internals::Elsewhere")));          // another block of the namespace
		TEST_ASSERT(scope.WhyOut(*TypeNamed(model, "Internals::Detail")).find("namespace block") != std::string::npos);
	}

	void ReflectionScopeTest::OutOfScopeLeavesTheImageAndTheThunks()
	{
		Built built;
		Build(built, kProgram);
		TEST_ASSERT(built.ok);
		const std::vector<std::string> names = ReflectedTypeNames(built.compiler.GetProgramModel());
		const auto listed = [&](const char* name) { return std::find(names.begin(), names.end(), name) != names.end(); };
		TEST_ASSERT(!listed("Hidden") && !listed("Internals::Detail") && !listed("Box<int32>"));
		TEST_ASSERT(listed("Shown") && listed("Internals::Exposed") && listed("Internals::Elsewhere"));
		std::string errors;
		std::vector<std::string> functions;
		TEST_ASSERT_MSG(Lowered(kProgram, errors, &functions), errors.c_str());
		TEST_ASSERT(!AnyStartsWithAndHolds(functions, "__c4_reflect_", "Hidden"));       // no construct, destroy or copy thunk
		// What reflection may call: the functions lowering made code for, in scope (Helper and Twice are lowered, out).
		ProgramLowering lowering(built.compiler.GetProgramModelForLowering(), LoweringOptions{});
		TEST_ASSERT(lowering.Run());
		std::vector<std::string> callable;
		for (const InvokeThunkUse& use : lowering.Program().invokeThunks) callable.push_back(use.target->name.c_str());
		const auto calls = [&](const char* name) { return std::find(callable.begin(), callable.end(), name) != callable.end(); };
		TEST_ASSERT(!calls("Helper") && !calls("Twice") && !calls("Secret"));
		TEST_ASSERT(calls("main"));
	}

	// A global out of scope has storage where code names it - in its own file's partition, though another partition's code
	// named it - and none where nothing does.
	void ReflectionScopeTest::AnOutOfScopeGlobalHasStorageOnlyWhereNamed()
	{
		CompilerInstance compiler;
		compiler.RegisterInMemory("lib.ch", "[reflect(\"off\")] int32 g_named = 3;\n[reflect(\"off\")] int32 g_unnamed = 4;\n");
		std::string main = "include \"lib.ch\"\nint32 main() { return g_named";
		for (int i = 0; i < 6; ++i)
		{
			const std::string name = "Use" + std::to_string(i);
			compiler.RegisterInMemory(("use" + std::to_string(i) + ".cf").c_str(), ("include \"lib.ch\"\nint32 " + name + "() { return g_named + " + std::to_string(i) + "; }\n").c_str());
			main = "int32 " + name + "();\n" + main + " + " + name + "()";
		}
		main += " - 21; }\n";
		compiler.RegisterInMemory("main.cf", main.c_str());
		compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
		compiler.CompileAll();
		TEST_ASSERT(compiler.AnalyzeProgram());
		for (PartitionCopies copies : { PartitionCopies::None, PartitionCopies::Small })
		{
			llvm::LLVMContext context;
			ProgramBackend driver(context);
			driver.SplitInto(4, copies);
			std::string error;
			TEST_ASSERT_MSG(driver.Lower(compiler.GetProgramModelForLowering(), "main", &error), error.c_str());
			int named = 0, unnamed = 0;
			for (unsigned i = 0; i < driver.PartitionCount(); ++i)
			{
				const llvm::GlobalVariable* a = driver.PartitionModule(i).getNamedGlobal("g_named");
				const llvm::GlobalVariable* b = driver.PartitionModule(i).getNamedGlobal("g_unnamed");
				named += a && !a->isDeclaration() ? 1 : 0;
				unnamed += b && !b->isDeclaration() ? 1 : 0;
			}
			TEST_ASSERT_EQ(named, 1);
			TEST_ASSERT_EQ(unnamed, 0);
		}
	}

	void ReflectionScopeTest::ASerializerOfAnOutOfScopeTypeIsAnError()
	{
		const std::string program = "include \"bedrock.ch\"\n[reflect(\"off\")] struct Secret { public int32 code; }\n"
			"struct Save { public int32 level; public Secret secret; }\n"
			"int32 main() { Save save; save.level = 3; return Serialization::ToJson(save).IsEmpty() ? 1 : 0; }\n";
		std::string errors;
		TEST_ASSERT(!Lowered(program, errors));
		TEST_ASSERT_MSG(errors.find("'Secret' is out of reflection's scope") != std::string::npos, errors.c_str());
		TEST_ASSERT_MSG(errors.find("ToJson<Save> reads it through reflection (Save.secret)") != std::string::npos, errors.c_str());
		std::string none;
		TEST_ASSERT_MSG(Lowered("include \"bedrock.ch\"\nstruct Save { public int32 level; }\n"
			"int32 main() { Save save; save.level = 3; return Serialization::ToJson(save).IsEmpty() ? 1 : 0; }\n", none), none.c_str());
	}

	void ReflectionScopeTest::AWrongReflectArgumentIsAnError()
	{
		std::string errors;
		TEST_ASSERT(!Lowered("[reflect(\"maybe\")] struct Unsure { public int32 v; }\nint32 main() { Unsure u; u.v = 0; return u.v; }\n", errors));
		TEST_ASSERT_MSG(errors.find("[reflect] takes \"on\" or \"off\"") != std::string::npos, errors.c_str());
	}
} // namespace Testing
