#include "ProgramLoweringTest.h"
#include "Backend/ProgramBackend.h"
#include "Compiler/CompilerInstance.h"
#include "Model/Types/QualifiedName.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <string>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		// `Less` needs `<` on T, and so does the virtual `Bad`.
		constexpr const char* kTemplates =
			"template<T> class Box\n"
			"{\n"
			"\tpublic T value;\n"
			"\tpublic bool Less(const Box<T>& other) { return value < other.value; }\n"
			"\tpublic T Get() { return value; }\n"
			"}\n"
			"template<T> class Shape\n"
			"{\n"
			"\tpublic T value;\n"
			"\tpublic virtual bool Bad() { return value < value; }\n"
			"\tpublic int32 Size() { return 1; }\n"
			"}\n"
			"struct Plain { public int32 x; }\n";

		ProgramModel& MakeModel(CompilerInstance& compiler, const std::string& source)
		{
			compiler.RegisterInMemory("t.cf", source.c_str());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			return compiler.GetProgramModelForLowering();
		}

		std::string NameOf(const FunctionRecord& record)
		{
			const std::string name = record.name.empty() ? record.mangledName.c_str() : record.name.c_str();
			return record.containingType ? std::string(QualifiedName(record.containingType).c_str()) + "::" + name : name;
		}

		bool Lowered(const LoweredProgram& program, const std::string& name)
		{
			for (const FunctionRecord* record : program.lowered)
				if (NameOf(*record) == name) return true;
			return false;
		}

		std::string Errors(const ProgramLowering& lowering)
		{
			std::string text;
			for (const CompilationError& error : lowering.Errors().GetErrors()) text += std::string(error.message.c_str()) + "\n";
			return text;
		}

		// The functions lowered, by name, in the order they were lowered (the units codegen emits).
		std::vector<std::string> UnitOrder(const std::string& source, unsigned threads)
		{
			CompilerInstance compiler;
			ProgramModel& model = MakeModel(compiler, source);
			LoweringOptions options;
			options.threads = threads;
			ProgramLowering lowering(model, options);
			lowering.Run();
			std::vector<std::string> order;
			for (const LoweredUnit& unit : lowering.Program().units)
				if (unit.tree->record) order.push_back(NameOf(*unit.tree->record));
			return order;
		}
	}

	void ProgramLoweringTest::AnUnusedMemberOfAnInstanceIsNotLowered()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, std::string(kTemplates) + "int32 main() { Box<Plain> b; return b.Get().x; }\n");
		ProgramLowering lowering(model, {});
		TEST_ASSERT(lowering.Run());
		TEST_ASSERT(Lowered(lowering.Program(), "main"));
		TEST_ASSERT(Lowered(lowering.Program(), "Box<Plain>::Get"));
		TEST_ASSERT(!Lowered(lowering.Program(), "Box<Plain>::Less"));
	}

	void ProgramLoweringTest::ACalledMemberIsLoweredWithItsUse()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, std::string(kTemplates) + "int32 main() { Box<Plain> b; return b.Less(b) ? 1 : 0; }\n");
		ProgramLowering lowering(model, {});
		TEST_ASSERT(!lowering.Run());
		TEST_ASSERT(Errors(lowering).find("(in 'Box<Plain>::Less', used at t.cf (14:30))") != std::string::npos);
	}

	void ProgramLoweringTest::AnObjectsVtableNeedsItsVirtualMembers()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, std::string(kTemplates) + "int32 main() { Shape<int32> s; return s.Size(); }\n");
		ProgramLowering lowering(model, {});
		TEST_ASSERT(lowering.Run());
		TEST_ASSERT(Lowered(lowering.Program(), "Shape<int32>::Bad"));
	}

	// Reflection can make every type it registers, an instance the program only names too (docs/runtime/02 section 2):
	// its construct thunk stores the vptrs, so the vtables' functions are the program's, and every virtual method has an
	// invoke thunk that dispatches through the object. A non-virtual member no code calls still is not lowered (D-16).
	void ProgramLoweringTest::AReflectedInstanceNeedsItsVirtualMembers()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, std::string(kTemplates) +
			"int32 Count(Shape<Plain>* s) { return s == null ? 0 : s->Size(); }\nint32 main() { return Count(null); }\n");
		ProgramLowering lowering(model, {});
		TEST_ASSERT(!lowering.Run());
		TEST_ASSERT(Lowered(lowering.Program(), "Shape<Plain>::Bad"));
		TEST_ASSERT(Errors(lowering).find("Shape<Plain>::Bad") != std::string::npos);
		bool constructs = false;
		for (const LoweredUnit& unit : lowering.Program().units)
			constructs = constructs || (unit.kind == UnitKind::TypeThunk && std::string(unit.tree->record->mangledName.c_str()).find("construct") != std::string::npos &&
				std::string(unit.tree->record->mangledName.c_str()).find("Shape") != std::string::npos);
		TEST_ASSERT(constructs);
	}

	// A global's storage starts with its vptrs set: its vtables' functions are the program's, emitted in a program of one
	// partition too.
	void ProgramLoweringTest::AGlobalsStorageNeedsItsVirtualMembers()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, std::string(kTemplates) + "Shape<int32> shape;\nint32 main() { return 0; }\n");
		llvm::LLVMContext context;
		ProgramBackend backend(context);
		TEST_ASSERT(backend.Lower(model, "main"));
		bool defined = false;
		for (const llvm::Function& function : *backend.module())
			defined = defined || (!function.isDeclaration() && function.getName().contains("Bad"));
		TEST_ASSERT(defined);
	}

	void ProgramLoweringTest::AFunctionPointerGlobalStartsAtTheFunctionItNames()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler,
			"int32 Twice(int32 x) { return x * 2; }\nfuncptr<int32(int32)> step = Twice;\nint32 main() { return step(2); }\n");
		llvm::LLVMContext context;
		ProgramBackend backend(context);
		TEST_ASSERT(backend.Lower(model, "main"));
		const llvm::GlobalVariable* step = backend.module()->getGlobalVariable("step", true);
		TEST_ASSERT(step != nullptr && step->hasInitializer());
		const auto* start = llvm::dyn_cast<llvm::Function>(step->getInitializer());
		TEST_ASSERT(start != nullptr && start->getName().starts_with("Twice"));
	}

	// One run reports a failed body and a failed global start: a step after a failure still runs.
	void ProgramLoweringTest::EveryStepRunsAfterAFailedBody()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler,
			"int32 Broken() { return missingInBody; }\nint32 start = missingInStart;\nint32 main() { return Broken() + start; }\n");
		ProgramLowering lowering(model, {});
		TEST_ASSERT(!lowering.Run());
		const std::string errors = Errors(lowering);
		TEST_ASSERT(errors.find("missingInBody") != std::string::npos);
		TEST_ASSERT(errors.find("missingInStart") != std::string::npos);
	}

	// Two functions allocate the type, one counter is lowered; a split program gives each partition its own copy.
	void ProgramLoweringTest::ACensusCounterIsLoweredOnce()
	{
		const std::string source =
			"class Buffer { public int64 size = 16; [operator(\"native_bytes\")] public int64 NativeBytes() const { return size; } }\n"
			"Buffer* First() { return new Buffer(); }\nBuffer* Second() { return new Buffer(); }\n"
			"int32 main() { return (int32)(First()->size + Second()->size); }\n";
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, source);
		ProgramLowering lowering(model, {});
		TEST_ASSERT(lowering.Run());
		TEST_ASSERT_EQ(lowering.Program().census.size(), size_t(1));
		TEST_ASSERT(lowering.Program().census[0].tree->complete);

		CompilerInstance split;
		ProgramModel& splitModel = MakeModel(split, source);
		llvm::LLVMContext context;
		ProgramBackend backend(context);
		backend.SplitInto(4, PartitionCopies::Small);
		TEST_ASSERT(backend.Lower(splitModel, "main"));
		unsigned defining = 0;
		for (unsigned i = 0; i < backend.PartitionCount(); ++i)
			for (const llvm::Function& function : backend.PartitionModule(i))
				if (function.getName().starts_with("__c4_native_bytes.") && !function.isDeclaration())
				{
					TEST_ASSERT(function.hasLocalLinkage());
					++defining;
				}
		TEST_ASSERT(defining >= 1);
	}

	void ProgramLoweringTest::TheOrderIsTheSameOnEveryThreadCount()
	{
		std::string source = std::string(kTemplates) + "template<T> T Larger(T a, T b) { return a > b ? a : b; }\n";
		std::string calls;
		for (int i = 0; i < 40; ++i)
		{
			const std::string n = std::to_string(i);
			source += "int32 F" + n + "(int32 x) { Shape<int32> s; Box<int64> b; funcptr<int32(int32)> f = [](int32 v) -> int32 { return v + " + n +
				"; }; return Larger(f(x), s.Size()) + (int32)b.Get(); }\n";
			calls += "F" + n + "(" + n + ") + ";
		}
		source += "int32 main() { return " + calls + "0; }\n";
		const std::vector<std::string> one = UnitOrder(source, 1);
		const std::vector<std::string> eight = UnitOrder(source, 8);
		TEST_ASSERT(one.size() > 40);
		TEST_ASSERT(one == eight);
	}

	// Lowering decides what the program has; codegen that names a function lowering did not lower is an internal error.
	void ProgramLoweringTest::CodegenRefusesToNameWhatWasNotLowered()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, std::string(kTemplates) + "int32 main() { Box<Plain> b; return b.Get().x; }\n");
		ProgramLowering lowering(model, {});
		TEST_ASSERT(lowering.Run());
		LoweredProgram& program = lowering.Program();
		for (auto record = program.lowered.begin(); record != program.lowered.end(); ++record)
			if (NameOf(**record) == "Box<Plain>::Get")
			{
				program.lowered.erase(record);
				break;
			}
		llvm::LLVMContext context;
		ProgramEmitter emitter(context, HostSystem());
		EmitSettings settings;
		settings.entryName = "main";
		std::string error;
		TEST_ASSERT(!emitter.Emit(model, program, settings, lowering.Pool(), &error));
		TEST_ASSERT(error.find("codegen names what lowering did not lower") != std::string::npos);
		TEST_ASSERT(error.find("Get") != std::string::npos);
	}
} // namespace Testing
