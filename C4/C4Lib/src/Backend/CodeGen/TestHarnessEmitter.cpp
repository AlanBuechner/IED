#include "Backend/CodeGen/TestHarnessEmitter.h"
#include "Model/Types/ObjectType.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
#include <algorithm>
#include <map>

namespace Scripting
{
	namespace
	{
		bool ByName(const TestRegistry::Test* a, const TestRegistry::Test* b)
		{
			return std::string(a->name.c_str()) < std::string(b->name.c_str());
		}
	}

	TestHarnessEmitter::TestHarnessEmitter(ModuleContext& mctx, const TestRegistry& tests, const TestHarnessOptions& options)
		: m_MCtx(mctx), m_Tests(tests), m_Options(options), m_B(mctx.context())
	{
	}

	bool TestHarnessEmitter::Emit()
	{
		// Free tests by name, then each class's tests (classes by name) inside that class's fixtures.
		std::vector<const TestRegistry::Test*> freeTests;
		std::map<std::string, std::pair<const ObjectType*, std::vector<const TestRegistry::Test*>>> byClass;
		for (const TestRegistry::Test& test : m_Tests.Tests())
		{
			if (!Own(test.function)) continue;
			if (!test.owner) { freeTests.push_back(&test); continue; }
			auto& entry = byClass[std::string(test.owner->GetName().c_str())];
			entry.first = test.owner;
			entry.second.push_back(&test);
		}
		std::sort(freeTests.begin(), freeTests.end(), ByName);
		if (freeTests.empty() && byClass.empty()) return false;

		llvm::LLVMContext& context = m_MCtx.context();
		auto* i32 = m_B.getInt32Ty();
		auto* ptr = llvm::PointerType::get(context, 0);
		m_Main = llvm::Function::Create(llvm::FunctionType::get(i32, { i32, ptr }, false), llvm::Function::ExternalLinkage, m_Options.entry, m_MCtx.module());
		m_B.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", m_Main));
		m_B.CreateCall(m_MCtx.RuntimeFunction("c4_test_configure", m_B.getVoidTy(), { i32, ptr }), { m_Main->getArg(0), m_Main->getArg(1) });
		RunFixture(m_Tests.GlobalFixtures().setup);
		for (const TestRegistry::Test* test : freeTests) RunCase(*test);
		for (auto& [name, entry] : byClass) RunClass(entry.first, entry.second);
		RunFixture(m_Tests.GlobalFixtures().teardown);

		llvm::Constant* json = m_Options.jsonPath.empty() ? static_cast<llvm::Constant*>(llvm::ConstantPointerNull::get(ptr)) : Text(m_Options.jsonPath);
		m_B.CreateCall(m_MCtx.RuntimeFunction("c4_test_summary", m_B.getVoidTy(), { ptr }), { json });
		m_B.CreateRet(m_B.CreateCall(m_MCtx.RuntimeFunction("c4_test_fail_count", i32, {})));
		return true;
	}

	void TestHarnessEmitter::RunClass(const ObjectType* owner, const std::vector<const TestRegistry::Test*>& tests)
	{
		std::vector<const TestRegistry::Test*> ordered = tests;
		std::sort(ordered.begin(), ordered.end(), ByName);
		const TestRegistry::Fixtures& fixtures = m_Tests.ClassFixtures(owner);
		RunFixture(fixtures.setup);
		for (const TestRegistry::Test* test : ordered) RunCase(*test);
		RunFixture(fixtures.teardown);
	}

	void TestHarnessEmitter::RunFixture(const FunctionRecord* fixture)
	{
		if (Own(fixture)) m_B.CreateCall(m_MCtx.GetOrCreateFunction(*fixture), {});
	}

	// if (c4_test_selected(name)) { begin(name, file, line); t = now; c4_test_run_case(test); done(name, now - t); }
	// The runner's catch-all fails a case that an exception leaves, and the run goes on (B37).
	void TestHarnessEmitter::RunCase(const TestRegistry::Test& test)
	{
		llvm::LLVMContext& context = m_MCtx.context();
		auto* i32 = m_B.getInt32Ty();
		auto* i64 = m_B.getInt64Ty();
		auto* ptr = llvm::PointerType::get(context, 0);
		auto* voidT = m_B.getVoidTy();
		llvm::Constant* name = Text(test.name.c_str());
		const std::string file = m_Options.fileOf ? m_Options.fileOf(test.sourceId) : std::string();

		llvm::Value* selected = m_B.CreateCall(m_MCtx.RuntimeFunction("c4_test_selected", i32, { ptr }), { name });
		auto* run = llvm::BasicBlock::Create(context, "test.run", m_Main);
		auto* next = llvm::BasicBlock::Create(context, "test.next", m_Main);
		m_B.CreateCondBr(m_B.CreateICmpNE(selected, m_B.getInt32(0)), run, next);

		m_B.SetInsertPoint(run);
		m_B.CreateCall(m_MCtx.RuntimeFunction("c4_test_begin", voidT, { ptr, ptr, i32 }), { name, Text(file), m_B.getInt32(test.line) });
		llvm::Function* now = m_MCtx.RuntimeFunction("c4_test_now_ns", i64, {});
		llvm::Value* start = m_B.CreateCall(now, {});
		m_B.CreateCall(m_MCtx.RuntimeFunction("c4_test_run_case", voidT, { ptr }), { m_MCtx.GetOrCreateFunction(*test.function) });
		llvm::Value* elapsed = m_B.CreateSub(m_B.CreateCall(now, {}), start);
		m_B.CreateCall(m_MCtx.RuntimeFunction("c4_test_case_done", voidT, { ptr, i64 }), { name, elapsed });
		m_B.CreateBr(next);
		m_B.SetInsertPoint(next);
	}

	// A NUL-terminated string the runtime reads, shared by every use of the same text.
	llvm::Constant* TestHarnessEmitter::Text(const std::string& text)
	{
		auto it = m_Texts.find(text);
		if (it != m_Texts.end()) return it->second;
		auto* bytes = llvm::ConstantDataArray::getString(m_MCtx.context(), text, true);
		auto* global = new llvm::GlobalVariable(m_MCtx.module(), bytes->getType(), true, llvm::GlobalValue::PrivateLinkage, bytes, "c4.test.str");
		m_Texts[text] = global;
		return global;
	}
} // namespace Scripting
