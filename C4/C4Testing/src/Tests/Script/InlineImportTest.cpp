#include "InlineImportTest.h"
#include "Script/HostAssumptions.h"
#include "Script/InlineImport.h"
#include <algorithm>
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
	using namespace C4Compiler;

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

		// A chunk of one body, `name`, which reads the first of the `fields` int32s of a struct named `type`.
		std::string StructChunk(const std::string& name, const std::string& type, unsigned fields)
		{
			llvm::LLVMContext context;
			llvm::Module module("inline bodies", context);
			llvm::StructType* record = llvm::StructType::create(context, std::vector<llvm::Type*>(fields, llvm::Type::getInt32Ty(context)), type);
			llvm::FunctionType* signature = llvm::FunctionType::get(llvm::Type::getInt32Ty(context), false);
			llvm::Function* function = llvm::Function::Create(signature, llvm::GlobalValue::ExternalLinkage, name, module);
			llvm::IRBuilder<> B(llvm::BasicBlock::Create(context, "entry", function));
			llvm::Value* slot = B.CreateAlloca(record);
			B.CreateRet(B.CreateLoad(B.getInt32Ty(), B.CreateStructGEP(record, slot, 0)));
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
		std::vector<std::string> imported;
		TEST_ASSERT(ImportInlineBodies(*script, chunks, index, imported, error));
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
		std::vector<std::string> imported;
		TEST_ASSERT(ImportInlineBodies(*script, chunks, index, imported, error));
		TEST_ASSERT(error.empty());
		std::sort(imported.begin(), imported.end());
		TEST_ASSERT(imported == (std::vector<std::string>{ "First", "Second" })); // what it took, which a cached script checks
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
		std::vector<std::string> imported;
		llvm::LLVMContext first;
		std::unique_ptr<llvm::Module> fromFirst = Calling(first, "Second");
		TEST_ASSERT(ImportInlineBodies(*fromFirst, chunks, index, imported, error));
		llvm::LLVMContext second;
		std::unique_ptr<llvm::Module> fromSecond = Calling(second, "First");
		TEST_ASSERT(ImportInlineBodies(*fromSecond, chunks, index, imported, error));
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
		std::vector<std::string> imported;
		TEST_ASSERT(ImportInlineBodies(*script, chunks, index, imported, error));
		TEST_ASSERT(imported.empty());
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
			std::vector<std::string> imported;
			TEST_ASSERT(!ImportInlineBodies(*script, chunks, index, imported, error));
			TEST_ASSERT(error.find("the interface's inline bodies do not read") != std::string::npos);
		}
		TEST_PASSED();
	}

	// A body's digest is its code alone: the same whatever else its chunk holds, another when its code changes.
	void InlineImportTest::ABodysDigestIsItsCodeAlone()
	{
		InlineBodyIndex alone;
		InlineBodyIndex beside;
		InlineBodyIndex changed;
		const std::vector<std::string> aloneChunks = { Chunk({ { "First", { "Second" } } }) };
		const std::vector<std::string> besideChunks = { Chunk({ { "Other", {} }, { "First", { "Second" } }, { "More", { "Other" } } }) };
		const std::vector<std::string> changedChunks = { Chunk({ { "First", { "Third" } } }) };
		std::string error;
		const std::string digest = alone.Digest("First", aloneChunks, error);
		TEST_ASSERT(!digest.empty() && error.empty());
		TEST_ASSERT(digest == beside.Digest("First", besideChunks, error));
		TEST_ASSERT(digest != changed.Digest("First", changedChunks, error)); // it calls another function now
		TEST_ASSERT(alone.Digest("Missing", aloneChunks, error).empty() && error.empty()); // no chunk holds it
		TEST_ASSERT(digest == alone.Digest("First", aloneChunks, error)); // made once, the same after
		TEST_PASSED();
	}

	// A digest does not follow the chunks read before it: two chunks naming a struct alike each keep the name (one context
	// would give the second `Pair.0`, and a cached script's check would fail by the order the leaves took their bodies in).
	void InlineImportTest::ADigestIsTheSameWhateverWasReadBefore()
	{
		const std::vector<std::string> chunks = { StructChunk("Before", "Pair", 3), StructChunk("After", "Pair", 2) };
		InlineBodyIndex fresh;
		InlineBodyIndex used;
		std::string error;
		TEST_ASSERT(!used.Digest("Before", chunks, error).empty());
		const std::string after = used.Digest("After", chunks, error);
		TEST_ASSERT(!after.empty() && error.empty());
		TEST_ASSERT(after == fresh.Digest("After", chunks, error));
		TEST_PASSED();
	}

	// A cached script that inlined a body is used while the host's body is the same.
	void InlineImportTest::AnInlinedBodyIsAnAssumption()
	{
		HostInterface host;
		host.inlineBodies = { Chunk({ { "First", { "Second" } } }) };
		const HostIndex index(host);
		InterfaceDigests digests(host);
		std::string error;
		HostAssumptions assumptions;
		assumptions.inlineBodies = { { "First", index.inlineBodies->Digest("First", host.inlineBodies, error) } };
		TEST_ASSERT(AssumptionsHold(assumptions, index, host.inlineBodies, digests));
		HostInterface rebuilt;
		rebuilt.inlineBodies = { Chunk({ { "First", {} } }) };
		const HostIndex rebuiltIndex(rebuilt);
		InterfaceDigests rebuiltDigests(rebuilt);
		TEST_ASSERT(!AssumptionsHold(assumptions, rebuiltIndex, rebuilt.inlineBodies, rebuiltDigests));
		HostInterface without;
		const HostIndex withoutIndex(without);
		InterfaceDigests withoutDigests(without);
		TEST_ASSERT(!AssumptionsHold(assumptions, withoutIndex, without.inlineBodies, withoutDigests)); // no longer inlined: it may have changed
		TEST_PASSED();
	}
} // namespace Testing
