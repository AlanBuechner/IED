#include "ParallelLoweringTest.h"
#include "Backend/CodeGen/CodeGen.h"
#include "Compiler/Toolchain.h"
#include "Compiler/WorkerPool.h"
#include "Model/ModelWrites.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		constexpr unsigned kThreads = 8;

		SymbolInfo Local(uint32 slot)
		{
			SymbolInfo info;
			info.kind = SymbolKind::Variable;
			info.localSlot = slot;
			return info;
		}

		// `work(t)` on kThreads threads started together.
		void OnThreads(const std::function<void(unsigned)>& work)
		{
			std::atomic<unsigned> ready = 0;
			std::vector<std::thread> threads;
			for (unsigned t = 0; t < kThreads; ++t)
				threads.emplace_back([&, t] {
					++ready;
					while (ready < kThreads) std::this_thread::yield();
					work(t);
				});
			for (std::thread& thread : threads) thread.join();
		}

		ProgramModel& MakeModel(CompilerInstance& compiler, const std::string& source, bool bedrock = false)
		{
			compiler.RegisterInMemory("t.cf", source.c_str());
			if (bedrock) compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			return compiler.GetProgramModelForLowering();
		}

		const FunctionRecord* Named(ProgramModel& model, const char* name)
		{
			Core::Vector<const FunctionRecord*> records;
			model.AllFunctionRecords(records);
			for (const FunctionRecord* record : records)
				if (record->name == name && record->bodyNode) return record;
			return nullptr;
		}

		// Whether `change` stops with ModelFrozen.
		bool Refused(const std::function<void()>& change)
		{
			try
			{
				change();
				return false;
			}
			catch (const ModelFrozen&)
			{
				return true;
			}
		}

		// Many functions that instantiate templates, make lambdas, and hold objects with compiler-written lifecycles.
		std::string ManyFunctions(int count)
		{
			std::string source = "include \"bedrock.ch\"\n"
				"struct Holder { public Core::String name; public int32 count; }\n"
				"template<T> T Larger(T a, T b) { return a > b ? a : b; }\n";
			std::string calls;
			for (int i = 0; i < count; ++i)
			{
				const std::string n = std::to_string(i);
				source += "int32 F" + n + "(int32 x) {\n"
					"\tHolder h; h.name = Core::String(\"" + n + "\"); h.count = x + " + n + ";\n"
					"\tCore::Array<int32> values; values.Push(x);\n"
					"\tfuncptr<int32(int32)> step = [](int32 v) -> int32 { return v + " + n + "; };\n"
					"\treturn Larger(step(h.count), values[0]) + (int32)Larger(" + n + ".5, 2.5) + (int32)Larger((int64)x, (int64)" + n + ");\n"
					"}\n";
				calls += "\tsum += F" + n + "(" + n + ");\n";
			}
			return source + "int32 main() {\n\tint32 sum = 0;\n" + calls + "\treturn sum;\n}\n";
		}

		struct Lowered
		{
			bool ok = false;
			std::string module;
			std::vector<std::string> errors;
		};

		Lowered LowerOn(const std::string& source, unsigned threads)
		{
			CompilerInstance compiler;
			ProgramModel& model = MakeModel(compiler, source, true);
			llvm::LLVMContext context;
			CodeGenDriver driver(context);
			driver.LowerOn(threads);
			Lowered lowered;
			lowered.ok = driver.Lower(model, "main");
			for (const CompilationError& error : driver.LoweringErrors()) lowered.errors.push_back(error.message.c_str());
			llvm::raw_string_ostream text(lowered.module);
			driver.module()->print(text, nullptr);
			return lowered;
		}
	}

	void ParallelLoweringTest::ScopeCursorsOnSeveralThreadsKeepTheirLocals()
	{
		SymbolTable table;
		Scope* root = table.GetRoot();
		const uint32 children = root->GetChildren().Count();
		std::atomic<int> wrong = 0;
		OnThreads([&](unsigned t) {
			for (uint32 round = 0; round < 2000; ++round)
			{
				ScopeCursor cursor(table);
				ScopeGuard function = table.PushScope(Scope::Kind::Function, "f");
				table.Current()->Register("x", Local(t));
				{
					ScopeGuard block = table.PushScope(Scope::Kind::Block, "b");
					table.Current()->Register("y", Local(round));
					const SymbolInfo* x = table.Current()->Find("x");
					const SymbolInfo* y = table.Current()->Find("y");
					if (table.Current() != block.GetScope() || !x || x->localSlot != t || !y || y->localSlot != round) ++wrong;
				}
				if (table.Current() != function.GetScope()) ++wrong;
			}
		});
		TEST_ASSERT_EQ(wrong.load(), 0);
		TEST_ASSERT(table.Current() == root);
		TEST_ASSERT_EQ(root->GetChildren().Count(), children); // a lowering's layers never join the tree
	}

	void ParallelLoweringTest::ALoweringInsideAnotherStartsWhereItIs()
	{
		SymbolTable table;
		ScopeCursor outer(table);
		ScopeGuard function = table.PushScope(Scope::Kind::Function, "outer");
		table.Current()->Register("bound", Local(7));
		{
			ScopeCursor inner(table);
			TEST_ASSERT(table.Current() == function.GetScope());
			ScopeGuard lambda = table.PushScope(Scope::Kind::Function, "lambda");
			TEST_ASSERT(lambda.GetScope()->GetParent() == function.GetScope());
			const SymbolInfo* bound = table.Current()->Find("bound");
			TEST_ASSERT(bound && bound->localSlot == 7);
		}
		TEST_ASSERT(table.Current() == function.GetScope());
	}

	void ParallelLoweringTest::InterningOnSeveralThreadsKeepsOneType()
	{
		TypeStore store;
		std::vector<std::vector<const Type*>> made(kThreads);
		OnThreads([&](unsigned t) {
			const Type* int32Type = store.Builtin(TypeKind::Int32);
			for (uint64 i = 1; i <= 300; ++i)
			{
				const Type* array = store.Array(int32Type, i);
				made[t].push_back(array);
				made[t].push_back(store.Pointer(array, i % 2 == 0));
				made[t].push_back(store.Reference(array));
				made[t].push_back(store.Function(int32Type, { array }));
				made[t].push_back(store.FunPtr(int32Type, { array, int32Type }));
			}
		});
		for (unsigned t = 1; t < kThreads; ++t) TEST_ASSERT(made[t] == made[0]);
		TEST_ASSERT(made[0][0] != made[0][5]); // int32[1] and int32[2] are two types
	}

	void ParallelLoweringTest::WorkerPoolRunsEveryIndexOncePerRound()
	{
		WorkerPool pool(kThreads);
		std::vector<std::atomic<int>> hits(500);
		for (int round = 1; round <= 200; ++round)
		{
			const size_t count = static_cast<size_t>(round) * 5 % 500 + 1;
			pool.Run(count, [&](size_t i) { ++hits[i]; });
			for (size_t i = 0; i < count; ++i)
				if (hits[i].exchange(0) != 1) TEST_ASSERT(false);
		}
	}

	void ParallelLoweringTest::AFrozenModelRefusesEveryChange()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, "struct S { public int32 v; }\nint32 main() { return 0; }\n");
		const ObjectType* s = nullptr;
		for (const ObjectType* type : model.ObjectTypes())
			if (type->GetName() == "S") s = type;
		TEST_ASSERT(s != nullptr);

		model.Writes().Freeze(true);
		TEST_ASSERT(Refused([&] {
			auto type = std::make_unique<ObjectType>(TypeKind::Object, "T");
			model.TypeStorePtr()->Own(type.get());
			type.release();
		}));
		TEST_ASSERT(Refused([&] { FunctionRecord record; record.mangledName = "F"; model.Symbols()->AddFunctionRecord(record); }));
		TEST_ASSERT(Refused([&] { model.Symbols()->CreateScope(Scope::Kind::Namespace, "N", nullptr); }));
		TEST_ASSERT(Refused([&] { model.Symbols()->GetRoot()->Register("name", Local(0)); }));
		TEST_ASSERT(Refused([&] { ObjectType::FieldInfo field; field.mName = "w"; const_cast<ObjectType*>(s)->AddField(field); }));
		TEST_ASSERT(Refused([&] { model.EnsureSynthesizedLifecycle(s, LifecycleOp::Copy); }));
		TEST_ASSERT(!Refused([&] { model.TypeStorePtr()->Pointer(s); })); // interning changes nothing lowering reads
		model.Writes().Freeze(false);
		TEST_ASSERT(model.EnsureSynthesizedLifecycle(s, LifecycleOp::Copy) != nullptr);
	}

	void ParallelLoweringTest::AStoppedLoweringNamesAnInstanceToMake()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler,
			"template<T> T Pick(T a, T b) { return a; }\nint32 Use() { return Pick(1, 2); }\nint32 main() { return Use(); }\n");
		const FunctionRecord* use = Named(model, "Use");
		TEST_ASSERT(use != nullptr);
		ErrorTracker errors;
		std::function<void()> make;
		model.Writes().Freeze(true);
		try
		{
			LowerFunction(model, errors, use);
		}
		catch (const ModelFrozen& frozen)
		{
			make = frozen.make;
		}
		model.Writes().Freeze(false);
		TEST_ASSERT(make != nullptr);
		make();
		model.Writes().Freeze(true);
		const bool refused = Refused([&] { TEST_ASSERT(LowerFunction(model, errors, use).complete); });
		model.Writes().Freeze(false);
		TEST_ASSERT(!refused); // the instance was made: the lowering finds it
		TEST_ASSERT(!errors.HasErrors());
	}

	void ParallelLoweringTest::LoweringOnEightThreadsBuildsTheSameModule()
	{
		const std::string source = ManyFunctions(60);
		const Lowered one = LowerOn(source, 1);
		const Lowered eight = LowerOn(source, kThreads);
		TEST_ASSERT(one.ok && eight.ok);
		TEST_ASSERT(one.module.find("F59") != std::string::npos);
		TEST_ASSERT(one.module == eight.module);
	}

	void ParallelLoweringTest::DiagnosticsKeepTheOrderOfTheFunctions()
	{
		std::string source;
		for (int i = 0; i < 40; ++i) source += "int32 A" + std::to_string(i) + "() { return missing" + std::to_string(i) + "; }\n";
		source += "int32 main() { return 0; }\n";
		const Lowered one = LowerOn(source, 1);
		const Lowered eight = LowerOn(source, kThreads);
		TEST_ASSERT(!one.ok && !eight.ok);
		TEST_ASSERT(one.errors == eight.errors);
		TEST_ASSERT_EQ(one.errors.size(), size_t(40));
		for (size_t i = 0; i < one.errors.size(); ++i)
			TEST_ASSERT(one.errors[i].find("missing" + std::to_string(i)) != std::string::npos);
	}
}
