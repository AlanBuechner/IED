#include "DebugInfoTest.h"
#include "Backend/CodeGen/CodeGenDriver.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/Toolchain.h"
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <set>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		const char* const kProgram =
			"struct Point { public int32 x; public int32 y; }\n"            // 1
			"enum class Mode : int32 { Off, On }\n"                          // 2
			"class Counter {\n"                                              // 3
			"\tpublic int32 count;\n"                                        // 4
			"\tpublic void Add(int32 by) { count += by; }\n"                 // 5
			"}\n"                                                            // 6
			"int32 Sum(int32[4] values) {\n"                                 // 7
			"\tint32 total = 0;\n"                                           // 8
			"\tfor (int32 i = 0; i < 4; ++i) total += values[i]++;\n"        // 9
			"\treturn total;\n"                                              // 10
			"}\n"                                                            // 11
			"int32 main() {\n"                                               // 12
			"\tPoint p; p.x = 1; Mode m = Mode::On;\n"                       // 13
			"\tCounter c; c.Add(p.x);\n"                                     // 14
			"\tint32[4] values = { 1, 2, 3, 4 };\n"                          // 15
			"\treturn Sum(values) + c.count + (int32)m;\n"                   // 16
			"}\n"                                                            // 17
			"namespace Tally { int32 g_count = 4; }\n"                       // 18
			"class Registry { public static int32 s_size = 2; }\n"           // 19
			"union Bits { struct { public uint16 low; public uint16 high; }; public int32 whole; }\n" // 20
			"int32 Apply(funcptr<int32(int32)> f, func<int32(int32)> g, Bits b) { return f(b.whole) + g(b.low); }\n" // 21
			"int32 Letter(const char8* text) { char8 first = text[0]; uint8 byte = (uint8)first; return (int32)byte; }\n" // 22
			"class Fault { public int32 code; }\n"                           // 23
			"int32 Recover() {\n"                                            // 24
			"\ttry { Fault f; f.code = 3; throw f; }\n"                      // 25
			"\tcatch (Fault e) { return e.code; }\n"                         // 26
			"\treturn 0;\n"                                                  // 27
			"}\n"                                                            // 28
			"int32 Nested() {\n"                                             // 29
			"\tint32 helper(int32 a) { return a * 2; }\n"                     // 30
			"\treturn helper(21);\n"                                          // 31
			"}\n";                                                           // 32

		// Lowers kProgram (with the default library, as C4.exe does) and hands the module to `inspect`.
		template <typename Inspect>
		bool WithModule(bool debugInfo, Inspect inspect)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("debug.cf", kProgram);
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			llvm::LLVMContext context;
			CodeGenDriver driver(context);
			if (debugInfo) driver.EnableDebugInfo();
			if (!driver.Lower(compiler.GetProgramModelForLowering(), "main")) return false;
			inspect(*driver.module());
			return true;
		}

		llvm::DISubprogram* Described(llvm::Module& module, const char* name)
		{
			for (llvm::Function& fn : module)
				if (llvm::DISubprogram* sp = fn.getSubprogram(); sp && sp->getName() == name) return sp;
			return nullptr;
		}

		std::set<std::string> VariablesOf(const llvm::DISubprogram* function)
		{
			std::set<std::string> names;
			for (const llvm::DINode* node : function->getRetainedNodes())
				if (const auto* variable = llvm::dyn_cast<llvm::DILocalVariable>(node)) names.insert(variable->getName().str());
			return names;
		}
	}

	void DebugInfoTest::TheModuleIsCodeView()
	{
		bool codeView = false, version = false, hashes = false;
		TEST_ASSERT(WithModule(true, [&](llvm::Module& m) {
			codeView = m.getModuleFlag("CodeView") != nullptr;
			version = m.getModuleFlag("Debug Info Version") != nullptr;
			hashes = m.getModuleFlag("CodeViewGHash") != nullptr; // the objects carry their types' hashes for the link
		}));
		TEST_ASSERT(codeView && version && hashes);
		TEST_PASSED();
	}

	void DebugInfoTest::AWrittenFunctionHasItsNameAndLine()
	{
		unsigned sumLine = 0, mainLine = 0;
		std::string file;
		TEST_ASSERT(WithModule(true, [&](llvm::Module& m) {
			if (auto* sum = Described(m, "Sum")) { sumLine = sum->getLine(); file = sum->getFilename().str(); }
			if (auto* main = Described(m, "main")) mainLine = main->getLine();
		}));
		TEST_ASSERT_EQ(sumLine, 7u);
		TEST_ASSERT_EQ(mainLine, 12u);
		TEST_ASSERT_EQ(file, std::string("debug.cf"));
		TEST_PASSED();
	}

	void DebugInfoTest::ParametersAndLocalsAreNamedButTemporariesAreNot()
	{
		std::set<std::string> sum, main;
		TEST_ASSERT(WithModule(true, [&](llvm::Module& m) {
			if (auto* f = Described(m, "Sum")) sum = VariablesOf(f);
			if (auto* f = Described(m, "main")) main = VariablesOf(f);
		}));
		TEST_ASSERT(sum == std::set<std::string>({ "values", "total", "i" })); // `values[i]++` holds its index in a temporary
		TEST_ASSERT(main == std::set<std::string>({ "p", "m", "c", "values" }));
		TEST_PASSED();
	}

	// BUG-232: a local function is named as the program spells it; BUG-228: naming it after its scope's lowering
	// is over does not crash.
	void DebugInfoTest::ALocalFunctionIsDescribed()
	{
		unsigned helperLine = 0;
		std::set<std::string> helper;
		TEST_ASSERT(WithModule(true, [&](llvm::Module& m) {
			if (auto* f = Described(m, "helper")) { helperLine = f->getLine(); helper = VariablesOf(f); }
		}));
		TEST_ASSERT_EQ(helperLine, 30u);
		TEST_ASSERT(helper == std::set<std::string>({ "a" }));
		TEST_PASSED();
	}

	void DebugInfoTest::AMethodHasThis()
	{
		std::set<std::string> add;
		TEST_ASSERT(WithModule(true, [&](llvm::Module& m) {
			if (auto* f = Described(m, "Counter::Add")) add = VariablesOf(f);
		}));
		TEST_ASSERT(add == std::set<std::string>({ "this", "by" }));
		TEST_PASSED();
	}

	void DebugInfoTest::EveryCallHasALine()
	{
		unsigned calls = 0, unlocated = 0;
		TEST_ASSERT(WithModule(true, [&](llvm::Module& m) {
			for (llvm::Function& fn : m)
			{
				if (!fn.getSubprogram()) continue;
				for (llvm::BasicBlock& block : fn)
					for (llvm::Instruction& instruction : block)
						if (llvm::isa<llvm::CallBase>(instruction)) { ++calls; if (!instruction.getDebugLoc()) ++unlocated; }
			}
		}));
		TEST_ASSERT(calls > 0);
		TEST_ASSERT_EQ(unlocated, 0u);
		TEST_PASSED();
	}

	void DebugInfoTest::ObjectsAndEnumsAreDescribed()
	{
		unsigned pointMembers = 0, modeValues = 0;
		TEST_ASSERT(WithModule(true, [&](llvm::Module& m) {
			llvm::DebugInfoFinder finder;
			finder.processModule(m);
			for (const llvm::DIType* type : finder.types())
				if (const auto* composite = llvm::dyn_cast<llvm::DICompositeType>(type))
				{
					if (composite->getName() == "Point") pointMembers = composite->getElements().size();
					if (composite->getName() == "Mode") modeValues = composite->getElements().size();
				}
		}));
		TEST_ASSERT_EQ(pointMembers, 2u);
		TEST_ASSERT_EQ(modeValues, 2u);
		TEST_PASSED();
	}

	void DebugInfoTest::UnionsAndCallablesAreDescribed()
	{
		std::vector<std::string> bits;
		uint64_t highOffset = 0;
		bool pointsAtCode = false, isUnion = false;
		std::vector<std::string> callable;
		TEST_ASSERT(WithModule(true, [&](llvm::Module& m) {
			llvm::DISubprogram* apply = Described(m, "Apply");
			if (!apply) return;
			llvm::DITypeRefArray signature = apply->getType()->getTypeArray();
			if (signature.size() != 4) return;
			if (const auto* pointer = llvm::dyn_cast_or_null<llvm::DIDerivedType>(signature[1]))
				pointsAtCode = pointer->getTag() == llvm::dwarf::DW_TAG_pointer_type && llvm::isa_and_nonnull<llvm::DISubroutineType>(pointer->getBaseType());
			if (const auto* func = llvm::dyn_cast_or_null<llvm::DICompositeType>(signature[2]))
				for (const llvm::DINode* member : func->getElements()) callable.push_back(llvm::cast<llvm::DIDerivedType>(member)->getName().str());
			if (const auto* alternatives = llvm::dyn_cast_or_null<llvm::DICompositeType>(signature[3]))
			{
				isUnion = alternatives->getTag() == llvm::dwarf::DW_TAG_union_type;
				for (const llvm::DINode* member : alternatives->getElements())
				{
					const auto* field = llvm::cast<llvm::DIDerivedType>(member);
					bits.push_back(field->getName().str());
					if (field->getName() == "high") highOffset = field->getOffsetInBits();
				}
			}
		}));
		TEST_ASSERT(pointsAtCode && isUnion);
		TEST_ASSERT(callable == std::vector<std::string>({ "code", "environment" }));
		TEST_ASSERT(bits == std::vector<std::string>({ "low", "high", "whole" }));
		TEST_ASSERT_EQ(highOffset, uint64_t(16));
		TEST_PASSED();
	}

	// A char8 is CodeView's narrow character (so a debugger shows a char8* as text), a uint8 stays a byte.
	void DebugInfoTest::Char8IsTheNarrowCharacter()
	{
		std::string first, byte;
		TEST_ASSERT(WithModule(true, [&](llvm::Module& m) {
			llvm::DISubprogram* letter = Described(m, "Letter");
			if (!letter) return;
			for (const llvm::DINode* node : letter->getRetainedNodes())
				if (const auto* variable = llvm::dyn_cast<llvm::DILocalVariable>(node))
				{
					if (variable->getName() == "first") first = variable->getType()->getName().str();
					if (variable->getName() == "byte") byte = variable->getType()->getName().str();
				}
		}));
		TEST_ASSERT_EQ(first, std::string("char"));
		TEST_ASSERT_EQ(byte, std::string("uint8"));
		TEST_PASSED();
	}

	// BUG-203: a handler's entry carried the try body's last line, so a debugger entering the catch showed it.
	void DebugInfoTest::ACatchHandlerStartsOnItsCatchLine()
	{
		unsigned padLine = 0, caughtLine = 0;
		TEST_ASSERT(WithModule(true, [&](llvm::Module& m) {
			for (llvm::Function& fn : m)
			{
				if (!fn.getSubprogram() || fn.getSubprogram()->getName() != "Recover") continue;
				for (llvm::BasicBlock& block : fn)
					for (llvm::Instruction& instruction : block)
						if (llvm::isa<llvm::CatchPadInst>(instruction) && instruction.getDebugLoc()) padLine = instruction.getDebugLoc().getLine();
				for (const llvm::DINode* node : fn.getSubprogram()->getRetainedNodes())
					if (const auto* variable = llvm::dyn_cast<llvm::DILocalVariable>(node); variable && variable->getName() == "e")
						caughtLine = variable->getLine();
			}
		}));
		TEST_ASSERT_EQ(padLine, 26u);
		TEST_ASSERT_EQ(caughtLine, 26u); // the caught value is declared by its catch
		TEST_PASSED();
	}

	void DebugInfoTest::GlobalsHaveTheirScopedNamesOnce()
	{
		std::multiset<std::string> names;
		TEST_ASSERT(WithModule(true, [&](llvm::Module& m) {
			llvm::DebugInfoFinder finder;
			finder.processModule(m);
			for (const llvm::DIGlobalVariableExpression* global : finder.global_variables())
				names.insert(global->getVariable()->getName().str());
		}));
		TEST_ASSERT_EQ(names.count("Tally::g_count"), size_t(1));
		TEST_ASSERT_EQ(names.count("Registry::s_size"), size_t(1));
		TEST_ASSERT_EQ(names.count("Tally_g_count"), size_t(0));
		TEST_PASSED();
	}

	void DebugInfoTest::WithoutDebugInfoThereIsNone()
	{
		bool any = true;
		TEST_ASSERT(WithModule(false, [&](llvm::Module& m) {
			any = m.getModuleFlag("CodeView") != nullptr;
			for (llvm::Function& fn : m) any = any || fn.getSubprogram() != nullptr;
		}));
		TEST_ASSERT(!any);
		TEST_PASSED();
	}

	// BUG-351: the linker's @main has no lines, so the program's main inlined into it had none either and a debugger's
	// breakpoints bound to an out-of-line copy that never ran. With or without -g, main stays a call of its own.
	void DebugInfoTest::TheEntryWrapperNeverInlinesMain()
	{
		for (const bool debugInfo : { true, false })
		{
			int calls = 0, noInline = 0;
			TEST_ASSERT(WithModule(debugInfo, [&](llvm::Module& m) {
				for (llvm::Instruction& instruction : m.getFunction("main")->getEntryBlock())
					if (const auto* call = llvm::dyn_cast<llvm::CallInst>(&instruction))
					{
						++calls;
						noInline += call->isNoInline() ? 1 : 0;
					}
			}));
			TEST_ASSERT_EQ(calls, 1);
			TEST_ASSERT_EQ(noInline, 1);
		}
		TEST_PASSED();
	}
} // namespace Testing
