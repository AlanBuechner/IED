#include "CodeViewLocationsTest.h"
#include "Compiler/CodeViewLocations.h"
#include <llvm/AsmParser/Parser.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DebugProgramInstruction.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/SourceMgr.h>
#include <memory>
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// One function's locations: sum is 0, then a value; factor only 7; step 0, then 1; seed 5, then poison; pair's
		// low half 0 and its high half a value.
		std::string ModuleText(bool codeView)
		{
			return std::string(
				"define i64 @f(i64 %n) !dbg !5 {\n"
				"entry:\n"
				"    #dbg_value(i64 0, !9, !DIExpression(), !12)\n"
				"    #dbg_value(i64 7, !10, !DIExpression(), !12)\n"
				"    #dbg_value(i64 0, !11, !DIExpression(), !12)\n"
				"    #dbg_value(i64 5, !13, !DIExpression(), !12)\n"
				"    #dbg_value(i32 0, !14, !DIExpression(DW_OP_LLVM_fragment, 0, 32), !12)\n"
				"  %x = add i64 %n, 1, !dbg !12\n"
				"  %y = trunc i64 %x to i32, !dbg !12\n"
				"    #dbg_value(i64 %x, !9, !DIExpression(), !12)\n"
				"    #dbg_value(i64 1, !11, !DIExpression(), !12)\n"
				"    #dbg_value(i64 poison, !13, !DIExpression(), !12)\n"
				"    #dbg_value(i32 %y, !14, !DIExpression(DW_OP_LLVM_fragment, 32, 32), !12)\n"
				"  ret i64 %x, !dbg !12\n"
				"}\n"
				"!llvm.dbg.cu = !{!0}\n")
				+ (codeView ? "!llvm.module.flags = !{!3, !4}\n" : "!llvm.module.flags = !{!4}\n") +
				"!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: !1, isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug)\n"
				"!1 = !DIFile(filename: \"t.cf\", directory: \"/\")\n"
				"!3 = !{i32 2, !\"CodeView\", i32 1}\n"
				"!4 = !{i32 2, !\"Debug Info Version\", i32 3}\n"
				"!5 = distinct !DISubprogram(name: \"f\", scope: !1, file: !1, line: 1, type: !6, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0)\n"
				"!6 = !DISubroutineType(types: !{null})\n"
				"!7 = !DIBasicType(name: \"int64\", size: 64, encoding: DW_ATE_signed)\n"
				"!9 = !DILocalVariable(name: \"sum\", scope: !5, file: !1, line: 2, type: !7)\n"
				"!10 = !DILocalVariable(name: \"factor\", scope: !5, file: !1, line: 3, type: !7)\n"
				"!11 = !DILocalVariable(name: \"step\", scope: !5, file: !1, line: 4, type: !7)\n"
				"!12 = !DILocation(line: 2, scope: !5)\n"
				"!13 = !DILocalVariable(name: \"seed\", scope: !5, file: !1, line: 5, type: !7)\n"
				"!14 = !DILocalVariable(name: \"pair\", scope: !5, file: !1, line: 6, type: !7)\n";
		}

		const char* KindOf(const llvm::DbgVariableRecord& record)
		{
			if (record.isKillLocation()) return "kill";
			for (const llvm::Value* operand : record.location_ops())
				if (!llvm::isa<llvm::ConstantInt>(operand)) return "value";
			return "const";
		}

		// A variable's locations in order, as "kill", "const" or "value" joined by commas.
		std::string LocationsOf(llvm::Module& module, const std::string& name)
		{
			std::string kinds;
			for (llvm::Instruction& instruction : module.getFunction("f")->getEntryBlock())
				for (llvm::DbgVariableRecord& record : llvm::filterDbgVars(instruction.getDbgRecordRange()))
					if (record.getVariable()->getName() == name) kinds += std::string(kinds.empty() ? "" : ",") + KindOf(record);
			return kinds;
		}

		// The locations of `name` once the module (with or without CodeView) has had its constants dropped.
		std::string AfterDropping(bool codeView, const std::string& name)
		{
			llvm::LLVMContext context;
			llvm::SMDiagnostic error;
			std::unique_ptr<llvm::Module> module = llvm::parseAssemblyString(ModuleText(codeView), error, context);
			if (!module) return "unparsed: " + error.getMessage().str();
			DropPartialConstantLocations(*module);
			return LocationsOf(*module, name);
		}
	}

	void CodeViewLocationsTest::AVariableWithAnotherLocationLosesItsConstants()
	{
		TEST_ASSERT_EQ(AfterDropping(true, "sum"), std::string("kill,value"));
		TEST_PASSED();
	}

	void CodeViewLocationsTest::AConstantVariableKeepsItsValue()
	{
		TEST_ASSERT_EQ(AfterDropping(true, "factor"), std::string("const"));
		TEST_PASSED();
	}

	void CodeViewLocationsTest::ConstantsThatDisagreeAreDropped()
	{
		TEST_ASSERT_EQ(AfterDropping(true, "step"), std::string("kill,kill"));
		TEST_PASSED();
	}

	void CodeViewLocationsTest::AKilledLocationCountsAsAnother()
	{
		TEST_ASSERT_EQ(AfterDropping(true, "seed"), std::string("kill,kill"));
		TEST_PASSED();
	}

	void CodeViewLocationsTest::AFragmentWithAnotherLocationLosesItsConstants()
	{
		TEST_ASSERT_EQ(AfterDropping(true, "pair"), std::string("kill,value"));
		TEST_PASSED();
	}

	void CodeViewLocationsTest::WithoutCodeViewNothingChanges()
	{
		TEST_ASSERT_EQ(AfterDropping(false, "sum"), std::string("const,value"));
		TEST_ASSERT_EQ(AfterDropping(false, "step"), std::string("const,const"));
		TEST_ASSERT_EQ(AfterDropping(false, "seed"), std::string("const,kill"));
		TEST_ASSERT_EQ(AfterDropping(false, "pair"), std::string("const,value"));
		TEST_PASSED();
	}
} // namespace Testing
