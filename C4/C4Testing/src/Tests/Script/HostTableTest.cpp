#include "HostTableTest.h"
#include "Backend/CodeGen/CodeGenDriver.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/Toolchain.h"
#include "Script/HostTable.h"
#include "Script/InlineBodies.h"
#include <algorithm>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/MemoryBuffer.h>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		const char* kProgram =
			"thread_local int32 t_frames = 0;\n"
			"int32 g_score = 0;\n"
			"int32 Add(int32 a, int32 b) { return a + b; }\n"
			"int32 Frame() { t_frames++; return t_frames; }\n"
			"const char* Greeting() { return \"hello\"; }\n"
			"int32 main() { g_score = Add(Frame(), 2); printf(\"%s\\n\", Greeting()); return 0; }\n";

		// Lowers `source` (with the prelude) and hands the module to `inspect` while it lives.
		template <typename Inspect>
		void Lowered(const std::string& source, Inspect inspect)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("t.cf", source);
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			llvm::LLVMContext context;
			CodeGenDriver driver(context);
			const bool ok = driver.Lower(compiler.GetProgramModelForLowering(), "main", nullptr);
			inspect(ok ? driver.module() : nullptr);
		}

		bool Lists(const std::vector<std::string>& names, const std::string& name)
		{
			return std::find(names.begin(), names.end(), name) != names.end();
		}

		std::unique_ptr<llvm::Module> ParseBodies(const std::string& bitcode, llvm::LLVMContext& context)
		{
			auto parsed = llvm::parseBitcodeFile(llvm::MemoryBufferRef(bitcode, "bodies"), context);
			if (!parsed) { llvm::consumeError(parsed.takeError()); return nullptr; }
			return std::move(*parsed);
		}

		bool Defines(const llvm::Module& module, const char* function)
		{
			const llvm::Function* f = module.getFunction(function);
			return f && !f->isDeclaration();
		}

		// The only chunk of a small program's inline bodies.
		std::string OneChunk(const std::vector<std::string>& chunks)
		{
			TEST_ASSERT_EQ(chunks.size(), size_t(1));
			return chunks.empty() ? std::string() : chunks.front();
		}

		// `count` functions returning text of their own, and a main calling each.
		std::string ManyTexts(int count)
		{
			std::string program, calls;
			for (int i = 0; i < count; ++i)
			{
				program += "const char* Text" + std::to_string(i) + "() { return \"text" + std::to_string(i) + "\"; }\n";
				calls += "printf(\"%s\", Text" + std::to_string(i) + "());\n";
			}
			return program + "int32 main() {\n" + calls + "return 0; }\n";
		}

		// The text of a private constant `module` defines, or "".
		std::vector<std::string> PrivateTexts(const llvm::Module& module)
		{
			std::vector<std::string> texts;
			for (const llvm::GlobalVariable& g : module.globals())
				if (g.hasPrivateLinkage() && g.hasInitializer())
					if (const auto* data = llvm::dyn_cast<llvm::ConstantDataArray>(g.getInitializer()); data && data->isCString())
						texts.push_back(data->getAsCString().str());
			return texts;
		}

		std::string ManyStatements()
		{
			std::string body = "int32 Long(int32 v) { int32 s = v;\n";
			for (int i = 0; i < 80; ++i) body += "s += v * " + std::to_string(i + 2) + ";\n";
			return body + "return s; }\n";
		}
	}

	void HostTableTest::AccessorsGiveTheCallingThreadsAddress()
	{
		Lowered(kProgram, [](llvm::Module* module) {
			TEST_ASSERT(module != nullptr);
			EmitThreadLocalAccessors(*module);
			const llvm::Function* accessor = module->getFunction("__c4_tls_t_frames");
			TEST_ASSERT(accessor != nullptr && !accessor->isDeclaration());
			bool readsThreadStorage = false;
			for (const llvm::BasicBlock& block : *accessor)
				for (const llvm::Instruction& instruction : block)
					if (const auto* call = llvm::dyn_cast<llvm::IntrinsicInst>(&instruction))
						readsThreadStorage |= call->getIntrinsicID() == llvm::Intrinsic::threadlocal_address;
			TEST_ASSERT(readsThreadStorage);
		});
		TEST_PASSED();
	}

	void HostTableTest::SymbolsListWhatAScriptMayName()
	{
		Lowered(kProgram, [](llvm::Module* module) {
			TEST_ASSERT(module != nullptr);
			EmitThreadLocalAccessors(*module);
			const std::vector<std::string> names = HostSymbols(*module, { "printf", "puts" });
			TEST_ASSERT(Lists(names, "Add_2int32int32"));
			TEST_ASSERT(Lists(names, "g_score"));
			TEST_ASSERT(Lists(names, "__c4_tls_t_frames"));
			TEST_ASSERT(Lists(names, "printf"));                  // used, and listed
			TEST_ASSERT(Lists(names, "puts"));                    // listed though unused
			TEST_ASSERT(Lists(names, "c4_script_throw"));         // a runtime entry point the host never calls
			TEST_ASSERT(Lists(names, "__CxxFrameHandler3"));
			TEST_ASSERT(module->getFunction("c4_script_throw") != nullptr);
			std::vector<std::string> sorted = names;
			std::sort(sorted.begin(), sorted.end());
			TEST_ASSERT(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end()); // each once
		});
		TEST_PASSED();
	}

	void HostTableTest::SymbolsLeaveOutPrivateAndThreadStorage()
	{
		Lowered(kProgram, [](llvm::Module* module) {
			TEST_ASSERT(module != nullptr);
			const std::vector<std::string> names = HostSymbols(*module, {});
			TEST_ASSERT(!Lists(names, "t_frames"));
			TEST_ASSERT(!Lists(names, "__c4_reflect_image") && !Lists(names, "__c4_gc_roots") && !Lists(names, "__ImageBase"));
			for (const std::string& name : names)
			{
				TEST_ASSERT(name.compare(0, 5, ".str.") != 0);
				TEST_ASSERT(name.compare(0, 5, "llvm.") != 0);
			}
		});
		TEST_PASSED();
	}

	void HostTableTest::TableReplacesTheDeclarationInOrder()
	{
		Lowered(kProgram, [](llvm::Module* module) {
			TEST_ASSERT(module != nullptr);
			// The scripting API's [link] declaration of the table, as a program that uses it has.
			auto* declared = new llvm::GlobalVariable(*module, llvm::Type::getInt8Ty(module->getContext()), false,
				llvm::GlobalValue::ExternalLinkage, nullptr, "__c4_script_table");
			auto* user = new llvm::GlobalVariable(*module, llvm::PointerType::get(module->getContext(), 0), true,
				llvm::GlobalValue::ExternalLinkage, declared, "test_table_user");
			const std::vector<std::string> names = HostSymbols(*module, {});
			std::string error;
			TEST_ASSERT(EmitHostTable(*module, names, &error));
			const llvm::GlobalVariable* table = module->getNamedGlobal("__c4_script_table");
			TEST_ASSERT(table != nullptr && table->hasInitializer() && table->isConstant());
			TEST_ASSERT(user->getInitializer() == table);
			const auto* fields = llvm::cast<llvm::ConstantStruct>(table->getInitializer());
			const auto* buildId = llvm::cast<llvm::GlobalVariable>(fields->getOperand(0));
			TEST_ASSERT(buildId->getName() == "__c4_script_build_id" && buildId->isDeclaration()); // the inline bodies come later
			std::array<uint8, 16> id{};
			id[0] = 0xAB;
			id[15] = 0xCD;
			EmitBuildId(*module, id);
			const auto* idBytes = llvm::cast<llvm::ConstantDataArray>(buildId->getInitializer());
			TEST_ASSERT(idBytes->getElementAsInteger(0) == 0xAB && idBytes->getElementAsInteger(15) == 0xCD);
			TEST_ASSERT_EQ(llvm::cast<llvm::ConstantInt>(fields->getOperand(1))->getZExtValue(), (uint64_t)names.size());
			const auto* entries = llvm::cast<llvm::ConstantArray>(fields->getOperand(2));
			for (size_t i = 0; i < names.size(); ++i) TEST_ASSERT(entries->getOperand((unsigned)i)->getName() == names[i]);
		});
		TEST_PASSED();
	}

	void HostTableTest::AMissingSymbolIsAnError()
	{
		Lowered(kProgram, [](llvm::Module* module) {
			TEST_ASSERT(module != nullptr);
			std::string error;
			TEST_ASSERT(!EmitHostTable(*module, { "Add_2int32int32", "Nowhere_0" }, &error));
			TEST_ASSERT(error.find("Nowhere_0") != std::string::npos);
		});
		TEST_PASSED();
	}

	void HostTableTest::SmallBodiesAreKept()
	{
		Lowered(kProgram, [](llvm::Module* module) {
			TEST_ASSERT(module != nullptr);
			const std::string bitcode = OneChunk(InlineBodies(*module, HostSymbols(*module, {})));
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> bodies = ParseBodies(bitcode, context);
			TEST_ASSERT(bodies != nullptr);
			TEST_ASSERT(Defines(*bodies, "Add_2int32int32"));
			TEST_ASSERT(bodies->getNamedGlobal("llvm.global_ctors") == nullptr);
		});
		TEST_PASSED();
	}

	void HostTableTest::LargeBodiesAndThreadStorageAreLeftOut()
	{
		Lowered(std::string(kProgram) + ManyStatements(), [](llvm::Module* module) {
			TEST_ASSERT(module != nullptr);
			const std::string bitcode = OneChunk(InlineBodies(*module, HostSymbols(*module, {})));
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> bodies = ParseBodies(bitcode, context);
			TEST_ASSERT(bodies != nullptr);
			TEST_ASSERT(!Defines(*bodies, "Long_1int32"));
			TEST_ASSERT(!Defines(*bodies, "Frame_0"));                       // it names a thread_local
			TEST_ASSERT(bodies->getNamedGlobal("t_frames") == nullptr || bodies->getNamedGlobal("t_frames")->isDeclaration());
		});
		TEST_PASSED();
	}

	void HostTableTest::ConstantsTravelWithTheirBodies()
	{
		Lowered(kProgram, [](llvm::Module* module) {
			TEST_ASSERT(module != nullptr);
			const std::string bitcode = OneChunk(InlineBodies(*module, HostSymbols(*module, {})));
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> bodies = ParseBodies(bitcode, context);
			TEST_ASSERT(bodies != nullptr && Defines(*bodies, "Greeting_0"));
			bool hasText = false;
			for (const llvm::GlobalVariable& g : bodies->globals())
				if (g.hasInitializer())
					if (const auto* data = llvm::dyn_cast<llvm::ConstantDataArray>(g.getInitializer()))
						hasText |= data->isCString() && data->getAsCString() == "hello" && g.hasPrivateLinkage();
			TEST_ASSERT(hasText);
		});
		TEST_PASSED();
	}

	// A chunk per kInlineBodiesPerGroup bodies, in the module's order, each with the constants its bodies name and no
	// other's.
	void HostTableTest::BodiesComeInChunksWithTheirConstants()
	{
		Lowered(ManyTexts(250), [](llvm::Module* module) {
			TEST_ASSERT(module != nullptr);
			const std::vector<std::string> chunks = InlineBodies(*module, HostSymbols(*module, {}), 64);
			TEST_ASSERT_EQ(chunks.size(), size_t(2));
			int defined = 0;
			for (const std::string& chunk : chunks)
			{
				llvm::LLVMContext context;
				std::unique_ptr<llvm::Module> bodies = ParseBodies(chunk, context);
				TEST_ASSERT(bodies != nullptr);
				std::vector<std::string> texts = PrivateTexts(*bodies);
				for (int i = 0; i < 250; ++i)
				{
					const std::string name = "Text" + std::to_string(i) + "_0";
					const bool here = Defines(*bodies, name.c_str());
					const bool text = std::find(texts.begin(), texts.end(), "text" + std::to_string(i)) != texts.end();
					TEST_ASSERT_EQ(here, text);
					defined += here ? 1 : 0;
				}
			}
			TEST_ASSERT_EQ(defined, 250);
			TEST_ASSERT_EQ(InlineBodies(*module, HostSymbols(*module, {}), 1).size(), size_t(1));
		});
		TEST_PASSED();
	}

	void HostTableTest::NoBodiesWithoutSymbols()
	{
		Lowered(kProgram, [](llvm::Module* module) {
			TEST_ASSERT(module != nullptr);
			TEST_ASSERT(InlineBodies(*module, {}).empty());
		});
		TEST_PASSED();
	}
} // namespace Testing
