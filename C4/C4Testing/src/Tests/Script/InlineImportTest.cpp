#include "InlineImportTest.h"
#include "Script/InlineImport.h"
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <string>
#include <utility>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		using Bodies = std::vector<std::pair<std::string, std::vector<std::string>>>; // each body's name and the bodies it calls

		// `name` in `module`: 1 plus what each of `calls` returns.
		void Define(llvm::Module& module, const std::string& name, const std::vector<std::string>& calls)
		{
			llvm::LLVMContext& context = module.getContext();
			llvm::FunctionType* type = llvm::FunctionType::get(llvm::Type::getInt32Ty(context), false);
			auto* function = llvm::cast<llvm::Function>(module.getOrInsertFunction(name, type).getCallee());
			llvm::IRBuilder<> B(llvm::BasicBlock::Create(context, "entry", function));
			llvm::Value* sum = B.getInt32(1);
			for (const std::string& callee : calls) sum = B.CreateAdd(sum, B.CreateCall(module.getOrInsertFunction(callee, type)));
			B.CreateRet(sum);
		}

		// A chunk of the interface's inline bodies, as bitcode.
		std::string Chunk(const Bodies& bodies)
		{
			llvm::LLVMContext context;
			llvm::Module module("inline bodies", context);
			for (const auto& [name, calls] : bodies) Define(module, name, calls);
			std::string bitcode;
			llvm::raw_string_ostream out(bitcode);
			llvm::WriteBitcodeToFile(module, out);
			out.flush();
			return bitcode;
		}

		// First calls Second calls Third, each in a chunk of its own; Unused beside First.
		std::vector<std::string> ThreeDeep()
		{
			return { Chunk({ { "First", { "Second" } }, { "Unused", {} } }), Chunk({ { "Second", { "Third" } } }), Chunk({ { "Third", {} } }) };
		}

		// A script whose Run calls `callee`.
		std::unique_ptr<llvm::Module> Calling(llvm::LLVMContext& context, const std::string& callee)
		{
			auto script = std::make_unique<llvm::Module>("script", context);
			Define(*script, "Run", { callee });
			return script;
		}

		bool Taken(const llvm::Module& script, const char* name)
		{
			const llvm::Function* function = script.getFunction(name);
			return function && !function->isDeclaration() && function->hasAvailableExternallyLinkage();
		}
	}

	void InlineImportTest::TheIndexLocatesEveryBody()
	{
		InlineBodyIndex index;
		const std::vector<std::string> chunks = ThreeDeep();
		std::string error;
		const llvm::StringMap<uint32>* where = index.Locate(chunks, error);
		TEST_ASSERT(where != nullptr);
		TEST_ASSERT(error.empty());
		TEST_ASSERT(where->size() == 4);
		TEST_ASSERT(where->lookup("First") == 0);
		TEST_ASSERT(where->lookup("Unused") == 0);
		TEST_ASSERT(where->lookup("Second") == 1);
		TEST_ASSERT(where->lookup("Third") == 2);
		TEST_ASSERT(!where->count("Run")); // only bodies: a name a chunk merely declares is in none
		TEST_PASSED();
	}

	// As a script's import found them before the index: by name, the last chunk's when two hold a body of one name.
	void InlineImportTest::TheLastChunkHoldingABodyGivesIt()
	{
		InlineBodyIndex index;
		const std::vector<std::string> chunks = { Chunk({ { "Twice", {} } }), Chunk({ { "Twice", { "Helper" } }, { "Helper", {} } }) };
		std::string error;
		const llvm::StringMap<uint32>* where = index.Locate(chunks, error);
		TEST_ASSERT(where != nullptr);
		TEST_ASSERT(where->lookup("Twice") == 1);

		llvm::LLVMContext context;
		std::unique_ptr<llvm::Module> script = Calling(context, "Twice");
		TEST_ASSERT(ImportInlineBodies(*script, chunks, index, error));
		TEST_ASSERT(Taken(*script, "Twice"));
		TEST_ASSERT(Taken(*script, "Helper")); // the last chunk's Twice calls it
		TEST_PASSED();
	}

	void InlineImportTest::AScriptTakesTheBodiesItReachesTwoCallsDeep()
	{
		InlineBodyIndex index;
		const std::vector<std::string> chunks = ThreeDeep();
		llvm::LLVMContext context;
		std::unique_ptr<llvm::Module> script = Calling(context, "First");
		std::string error;
		TEST_ASSERT(ImportInlineBodies(*script, chunks, index, error));
		TEST_ASSERT(error.empty());
		TEST_ASSERT(Taken(*script, "First"));
		TEST_ASSERT(Taken(*script, "Second"));
		TEST_ASSERT(script->getFunction("Third") && script->getFunction("Third")->isDeclaration()); // three calls deep: the host's code
		TEST_ASSERT(!script->getFunction("Unused")); // reached by nothing
		TEST_ASSERT(!script->getFunction("Run")->isDeclaration() && !script->getFunction("Run")->hasAvailableExternallyLinkage());
		TEST_PASSED();
	}

	// One index for every compile against the interface, each in its own context.
	void InlineImportTest::ScriptsShareOneIndex()
	{
		InlineBodyIndex index;
		const std::vector<std::string> chunks = ThreeDeep();
		std::string error;
		llvm::LLVMContext first;
		std::unique_ptr<llvm::Module> fromFirst = Calling(first, "Second");
		TEST_ASSERT(ImportInlineBodies(*fromFirst, chunks, index, error));
		llvm::LLVMContext second;
		std::unique_ptr<llvm::Module> fromSecond = Calling(second, "First");
		TEST_ASSERT(ImportInlineBodies(*fromSecond, chunks, index, error));
		TEST_ASSERT(Taken(*fromFirst, "Second") && Taken(*fromFirst, "Third") && !fromFirst->getFunction("First"));
		TEST_ASSERT(Taken(*fromSecond, "First") && Taken(*fromSecond, "Second"));
		TEST_PASSED();
	}

	void InlineImportTest::AScriptCallingNoBodyTakesNone()
	{
		InlineBodyIndex index;
		const std::vector<std::string> chunks = ThreeDeep();
		llvm::LLVMContext context;
		std::unique_ptr<llvm::Module> script = Calling(context, "Elsewhere");
		std::string error;
		TEST_ASSERT(ImportInlineBodies(*script, chunks, index, error));
		size_t defined = 0;
		for (const llvm::Function& function : *script) defined += function.isDeclaration() ? 0 : 1;
		TEST_ASSERT_EQ(defined, (size_t)1); // Run alone
		TEST_PASSED();
	}

	// Said by every compile that asks, though read once.
	void InlineImportTest::ADamagedChunkIsSaid()
	{
		InlineBodyIndex index;
		const std::vector<std::string> chunks = { Chunk({ { "First", {} } }), std::string("not bitcode") };
		llvm::LLVMContext context;
		for (int attempt = 0; attempt < 2; ++attempt)
		{
			std::unique_ptr<llvm::Module> script = Calling(context, "First");
			std::string error;
			TEST_ASSERT(!ImportInlineBodies(*script, chunks, index, error));
			TEST_ASSERT(error.find("the interface's inline bodies do not read") != std::string::npos);
		}
		TEST_PASSED();
	}
} // namespace Testing
