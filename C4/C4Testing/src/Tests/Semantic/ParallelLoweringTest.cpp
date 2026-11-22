#include "ParallelLoweringTest.h"
#include "Backend/CodeGen/CodeGen.h"
#include "Backend/Lowering/ParallelLowering.h"
#include "Backend/ProgramBackend.h"
#include "Compiler/Toolchain.h"
#include "Compiler/WorkerPool.h"
#include "Model/Concurrent/MadeTables.h"
#include "Model/ModelMaking.h"
#include <algorithm>
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
		constexpr int kRepeats = 3; // lowerings on threads compared with the one on one thread

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

		// Waits on this thread until `value` is `wanted`.
		void Await(const std::atomic<int>& value, int wanted)
		{
			while (value.load(std::memory_order_acquire) != wanted) std::this_thread::yield();
		}

		ProgramModel& MakeModel(CompilerInstance& compiler, const std::string& source, bool bedrock = false)
		{
			compiler.RegisterInMemory("t.cf", source.c_str());
			if (bedrock) compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			return compiler.GetProgramModelForLowering();
		}

		std::vector<const FunctionRecord*> Records(ProgramModel& model, const std::function<bool(const FunctionRecord&)>& wanted)
		{
			Core::Vector<const FunctionRecord*> records;
			model.AllFunctionRecords(records);
			std::vector<const FunctionRecord*> out;
			for (const FunctionRecord* record : records)
				if (wanted(*record)) out.push_back(record);
			return out;
		}

		const FunctionRecord* Named(ProgramModel& model, const char* name)
		{
			const auto found = Records(model, [&](const FunctionRecord& r) { return r.name == name && r.bodyNode; });
			return found.empty() ? nullptr : found[0];
		}

		const ObjectType* TypeNamed(ProgramModel& model, const char* name)
		{
			for (const ObjectType* type : model.ObjectTypes())
				if (type->GetName() == name) return type;
			return nullptr;
		}

		bool Contains(const std::vector<std::string>& list, const char* text)
		{
			return std::any_of(list.begin(), list.end(), [&](const std::string& s) { return s.find(text) != std::string::npos; });
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

		// Functions that all ask for the same instances at once, of every kind a lowering makes: class templates with
		// nested types and nested templates, member and extension templates, templates declared ahead of their
		// definitions, return lists, closures, local types, compiler-written lifecycles.
		std::string SameInstancesEverywhere(int count)
		{
			std::string source = "include \"bedrock.ch\"\n"
				"template<T> struct Outer {\n"
				"\tstruct Inner { public T value; public Core::String label; }\n"
				"\ttemplate<U> struct Pair { public T first; public U second; }\n"
				"\tpublic Inner inner;\n"
				"\ttemplate<U> public U Scale(U u) { return (U)(u * (U)2); }\n"
				"}\n"
				"template<T> struct Two { public T first; public T second; }\n"
				"template<T> T Sum() extends Two<T> { return first + second; }\n"
				"template<T> T Twice(T v);\n"
				"template<T> [T, int32] Split(T v) { return [v, 1]; }\n";
			std::string calls;
			for (int i = 0; i < count; ++i)
			{
				const std::string n = std::to_string(i);
				const std::string kind = i % 3 == 0 ? "int32" : i % 3 == 1 ? "int64" : "double";
				source += "int32 G" + n + "(int32 x) {\n"
					"\tstruct Local { public int32 a; public Core::String b; }\n"
					"\tLocal local; local.a = x;\n"
					"\tOuter<" + kind + "> outer; outer.inner.value = (" + kind + ")x;\n"
					"\tOuter<" + kind + ">::Pair<int16> pair; pair.second = (int16)" + n + ";\n"
					"\tTwo<" + kind + "> two; two.first = (" + kind + ")x; two.second = (" + kind + ")" + n + ";\n"
					"\tCore::Array<Outer<" + kind + ">::Inner> inners; inners.Push(outer.inner);\n"
					"\tfunc<int32(int32)> add = [x](int32 v) -> int32 { return v + x; };\n"
					"\t[" + kind + " sum, int32 one] = Split(two.Sum());\n"
					"\treturn add(local.a) + (int32)Twice(sum) + one + (int32)outer.Scale(pair.second) + (int32)inners.Count();\n"
					"}\n";
				calls += "\tsum += G" + n + "(" + n + ");\n";
			}
			source += "template<T> T Twice(T v) { return v + v; }\n";
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
			ProgramBackend driver(context);
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

	void ParallelLoweringTest::ARunInsideARunRunsOnItsThread()
	{
		WorkerPool pool(kThreads);
		std::vector<std::atomic<int>> hits(64 * 32);
		pool.Run(64, [&](size_t outer) {
			const std::thread::id runner = std::this_thread::get_id();
			pool.Run(32, [&](size_t inner) {
				if (std::this_thread::get_id() == runner) ++hits[outer * 32 + inner];
			});
		});
		for (std::atomic<int>& hit : hits) TEST_ASSERT_EQ(hit.load(), 1);
	}

	// The thread making an answer finds it at once (an instance names itself); every other thread finds it only when
	// the outermost request has ended, whole. A forgotten answer (a failed instance) is never shown.
	void ParallelLoweringTest::ARequestIsShownWhenItsOutermostRequestEnds()
	{
		ModelMaking making;
		MadeMap<int, int> answers(making, "an answer");
		std::atomic<int> step = 0;
		bool makerFoundItsOwn = false;
		bool makerFoundTheForgotten = true;
		std::thread maker([&] {
			{
				const ModelMaking::Request outer(making, "outer");
				{
					const ModelMaking::Request inner(making, "inner");
					answers.Add(1, 10);
					answers.Add(2, 20);
					answers.Forget(2);
				}
				makerFoundItsOwn = answers.Find(1) && *answers.Find(1) == 10;
				makerFoundTheForgotten = answers.Find(2) != nullptr;
				step.store(1, std::memory_order_release);
				Await(step, 2);
			}
			step.store(3, std::memory_order_release);
		});
		Await(step, 1);
		const bool seenBeforeTheEnd = answers.Find(1) != nullptr;
		step.store(2, std::memory_order_release);
		Await(step, 3);
		maker.join();
		TEST_ASSERT(makerFoundItsOwn);
		TEST_ASSERT(!makerFoundTheForgotten);
		TEST_ASSERT(!seenBeforeTheEnd);
		TEST_ASSERT(answers.Find(1) && *answers.Find(1) == 10);
		TEST_ASSERT(answers.Find(2) == nullptr);
		TEST_ASSERT_EQ(making.Requests().at("outer"), uint64(1));
		TEST_ASSERT_EQ(making.Requests().at("inner"), uint64(1));
	}

	// A list is walked in the order added: what the request in progress added comes after what was shown, for its own
	// thread only, and joins the end when it is shown. An element never moves.
	void ParallelLoweringTest::AListAddedToInARequestIsShownInItsOrder()
	{
		ModelMaking making;
		MadeLists<std::string, int> lists(making, "a list");
		const int& first = lists.Add("a", 1);
		std::atomic<int> step = 0;
		std::vector<int> makerSaw;
		std::thread maker([&] {
			{
				const ModelMaking::Request request(making, "adding");
				lists.Add("a", 2);
				lists.Add("b", 3);
				lists.ForEach("a", [&](int v) { makerSaw.push_back(v); });
				step.store(1, std::memory_order_release);
				Await(step, 2);
			}
			step.store(3, std::memory_order_release);
		});
		Await(step, 1);
		std::vector<int> othersSaw;
		lists.ForEach("a", [&](int v) { othersSaw.push_back(v); });
		const bool bSeen = lists.Has("b");
		step.store(2, std::memory_order_release);
		Await(step, 3);
		maker.join();
		TEST_ASSERT(makerSaw == std::vector<int>({ 1, 2 }));
		TEST_ASSERT(othersSaw == std::vector<int>({ 1 }));
		TEST_ASSERT(!bSeen);
		std::vector<int> after;
		lists.ForEach("a", [&](int v) { after.push_back(v); });
		TEST_ASSERT(after == std::vector<int>({ 1, 2 }));
		TEST_ASSERT(lists.First("b") && *lists.First("b") == 3);
		TEST_ASSERT(lists.First("a") == &first);
	}

	// While lowerings share the model a type or scope they read never changes, and a table changes only inside a request:
	// anything else is a race the driver reports (ProgramLowering, an internal error). A request and interning are no
	// violation, and outside a sharing nothing is.
	void ParallelLoweringTest::AChangeOutsideARequestIsAViolationWhileLoweringsShare()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, "struct S { public int32 v; }\nint32 main() { return 0; }\n");
		const ObjectType* s = TypeNamed(model, "S");
		TEST_ASSERT(s != nullptr);
		const FunctionRecord* copy = nullptr;
		{
			const ProgramModel::SharedLowering sharing(model);
			model.Symbols()->GetRoot()->Register("name", Local(0));
			ObjectType::FieldInfo field;
			field.mName = "w";
			const_cast<ObjectType*>(s)->AddField(field);
			copy = model.EnsureSynthesizedLifecycle(s, LifecycleOp::Copy);
			model.TypeStorePtr()->Pointer(s);
		}
		const std::vector<std::string> violations = model.Making().TakeViolations();
		TEST_ASSERT_EQ(violations.size(), size_t(2));
		TEST_ASSERT(Contains(violations, "a name in a scope lowerings read"));
		TEST_ASSERT(Contains(violations, "a field"));
		TEST_ASSERT(model.Making().TakeViolations().empty());
		TEST_ASSERT(copy != nullptr && copy == model.SynthesizedLifecycle(s, LifecycleOp::Copy));
		TEST_ASSERT_EQ(std::string(copy->mangledName.c_str()), std::string("__c4_copy_S"));
		model.Symbols()->GetRoot()->Register("other", Local(1));
		TEST_ASSERT(model.Making().TakeViolations().empty());
	}

	// Every body asks for the same instances at once: each is made once, by whichever asks first, and found whole by the
	// others, which lower to the end without a second try.
	void ParallelLoweringTest::EveryLoweringThatAsksFindsTheOneInstance()
	{
		std::string source = "template<T> struct Box { public T value; public T Get() const { return value; } }\n"
			"template<T> T Pick(T a, T b) { return a; }\n";
		for (int i = 0; i < 64; ++i)
			source += "int32 U" + std::to_string(i) + "() { Box<int32> b; b.value = Pick(" + std::to_string(i) + ", 1); return b.Get(); }\n";
		source += "int32 main() { return 0; }\n";
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, source);
		const std::vector<const FunctionRecord*> users = Records(model, [](const FunctionRecord& r) { return r.name.rfind("U", 0) == 0 && r.bodyNode; });
		TEST_ASSERT_EQ(users.size(), size_t(64));
		ParallelLowering lowering(kThreads);
		const std::vector<LoweredBody> bodies = lowering.Lower(model, users);
		for (const LoweredBody& body : bodies) TEST_ASSERT(body.tree->complete && !body.errors.HasErrors());
		TEST_ASSERT(lowering.Violations().empty());
		TEST_ASSERT_EQ(lowering.Stats().bodies, uint64_t(64));
		TEST_ASSERT_EQ(Records(model, [](const FunctionRecord& r) { return r.mangledName.rfind("Pick_Tint32_E", 0) == 0; }).size(), size_t(1));
		TEST_ASSERT_EQ(Records(model, [](const FunctionRecord& r) { return r.mangledName.rfind("Box_Tint32_E_Get", 0) == 0; }).size(), size_t(1));
		uint32 instances = 0; // the template's own type is named Box too: only an instance has arguments
		for (const ObjectType* type : model.ObjectTypes()) instances += type->GetName() == "Box" && !type->GetTemplateArguments().Empty() ? 1 : 0;
		TEST_ASSERT_EQ(instances, 1u);
	}

	// A body's lambdas are made by its one lowering, numbered in its order; lowering it again finds them.
	void ParallelLoweringTest::ALoweringMakesItsClosuresInItsOrder()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler,
			"int32 Use(int32 x) {\n"
			"\tfuncptr<int32(int32)> first = [](int32 v) -> int32 { return v + 1; };\n"
			"\tfuncptr<int32(int32)> second = [](int32 v) -> int32 { return v * 2; };\n"
			"\treturn first(second(x));\n"
			"}\nint32 main() { return Use(3); }\n");
		const FunctionRecord* use = Named(model, "Use");
		TEST_ASSERT(use != nullptr);
		const uint32 before = model.ObjectTypes().Count();
		ParallelLowering lowering(kThreads);
		TEST_ASSERT(lowering.Lower(model, { use })[0].tree->complete);
		TEST_ASSERT_EQ(model.ObjectTypes().Count(), before + 2);
		std::vector<std::string> made;
		for (uint32 i = before; i < model.ObjectTypes().Count(); ++i) made.push_back(model.ObjectTypes()[i]->GetName().c_str());
		TEST_ASSERT(made[0].size() > 2 && made[0].substr(made[0].size() - 2) == "_0");
		TEST_ASSERT(made[1].size() > 2 && made[1].substr(made[1].size() - 2) == "_1");
		TEST_ASSERT_EQ(lowering.Stats().requests.at("a closure"), uint64_t(2));
		TEST_ASSERT(ParallelLowering(kThreads).Lower(model, { use })[0].tree->complete);
		TEST_ASSERT_EQ(model.ObjectTypes().Count(), before + 2);
	}

	// A template declared ahead of its definition has two instances of one symbol (the declaration's, without a body,
	// and the definition's): which is `_1` follows from where they are declared, never from which thread asked first.
	void ParallelLoweringTest::RecordsMadeOnThreadsAreNamedInOneOrder()
	{
		std::string source = "template<T> T Ahead(T v);\n";
		for (int i = 0; i < 48; ++i)
			source += "int32 A" + std::to_string(i) + "() { return Ahead(" + std::to_string(i) + ") + (int32)Ahead((int64)" + std::to_string(i) + "); }\n";
		source += "template<T> T Ahead(T v) { return v + (T)1; }\nint32 main() { return A0(); }\n";
		const Lowered one = LowerOn(source, 1);
		TEST_ASSERT(one.ok);
		TEST_ASSERT(one.module.find("Ahead_Tint32_E") != std::string::npos);
		for (int repeat = 0; repeat < kRepeats; ++repeat)
		{
			const Lowered eight = LowerOn(source, kThreads);
			TEST_ASSERT(eight.ok);
			TEST_ASSERT(one.module == eight.module);
		}
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

	// The race this design must not have: many bodies asking for the same instances of every kind at once.
	void ParallelLoweringTest::ManyLoweringsAskForTheSameInstancesAtOnce()
	{
		const std::string source = SameInstancesEverywhere(72);
		const Lowered one = LowerOn(source, 1);
		TEST_ASSERT(one.ok);
		for (const std::string& error : one.errors) TEST_ASSERT_EQ(error, std::string());
		TEST_ASSERT(one.module.find("G71") != std::string::npos);
		for (int repeat = 0; repeat < kRepeats; ++repeat)
		{
			const Lowered eight = LowerOn(source, kThreads);
			TEST_ASSERT(eight.ok);
			TEST_ASSERT(one.module == eight.module);
		}
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
