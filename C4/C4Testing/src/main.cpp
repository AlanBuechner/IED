#define IMPLEMENT_LOGGER
#include "Core/Core.h"
#include "UnitTesting/Testing.h"
#include "UnitTesting/CrashHandler.h"
#include "Reflection.h"

// Force reflection registry initialization from generated.cpp before main().
// On Windows, static init order across TUs is undefined; this function pointer trick
// ensures the generated translation unit's static constructors run first.
#define PROJECT_NAME C4Testing
LINK_REFLECTION_DATA

// Front end: lexer, source manager, parser.
#include "Tests/Lexer/TokenBasicsTest.h"
#include "Tests/Lexer/SingleCharOperatorTest.h"
#include "Tests/Lexer/CompoundOperatorTest.h"
#include "Tests/Lexer/IdentifierTokenizationTest.h"
#include "Tests/Lexer/CommentTokenizationTest.h"
#include "Tests/Lexer/SourceLocationTrackingTest.h"
#include "Tests/Lexer/LexerFlowTest.h"
#include "Tests/Lexer/EndOfFileTest.h"
#include "Tests/Lexer/UndoMechanismTest.h"
#include "Tests/Lexer/LexerNestedUndoTest.h"
#include "Tests/Lexer/LexerRecoveryTest.h"
#include "Tests/SourceManager/SourceTest.h"
#include "Tests/Core/StringFormatTest.h"
#include "Tests/Parser/Foundation/AnnotationTest.h"
#include "Tests/Parser/Foundation/NamespaceQualifiedNameTest.h"
#include "Tests/Parser/Foundation/TemplateDefinitionTest.h"
#include "Tests/Parser/Foundation/QualTypeTest.h"
#include "Tests/Parser/Expressions/IdentifierExpressionTest.h"
#include "Tests/Parser/Expressions/LiteralTest.h"
#include "Tests/Parser/Expressions/PrattParserTest.h"
#include "Tests/Parser/Foundation/ParameterTest.h"
#include "Tests/Parser/Statements/CodeBlockTest.h"
#include "Tests/Parser/Statements/ExpressionStatementTest.h"
#include "Tests/Parser/Declarations/VariableDefinitionTest.h"
#include "Tests/Parser/Declarations/FunctionDefinitionTest.h"
#include "Tests/Parser/Declarations/ObjectDefinitionTest.h"
#include "Tests/Parser/Declarations/FunPtrTypeTest.h"
#include "Tests/Parser/Integration/FullProgramTest.h"

// The new ProgramModel: query API, overload resolution, and the simulated-lowering suite.
#include "Tests/Semantic/ProgramModelTest.h"
#include "Tests/Semantic/OverloadResolutionTest.h"
#include "Tests/Semantic/ModelParityTest.h"
#include "Tests/Semantic/SimulatedLoweringTest.h"
#include "Tests/Semantic/ProgramModelEdgeCasesTest.h"
#include "Tests/Semantic/DeclarationNodesTest.h"
#include "Tests/Semantic/ReflectedAnnotationsTest.h"
#include "Tests/Semantic/HIRLoweringTest.h"
#include "Tests/Semantic/CodeGenTest.h"
#include "Tests/Semantic/IsolatedCorpusModelTest.h"
#include "Tests/Semantic/ObjectModelTest.h"
#include "Tests/Semantic/LifecycleTest.h"
#include "Tests/Semantic/TemplateDeductionTest.h"
#include "Tests/Semantic/PreludeTest.h"
#include "Tests/Semantic/ScopeGuardTest.h"
#include "Tests/Semantic/ConstEvaluatorTest.h"
#include "Tests/Compiler/ErrorTrackerTest.h"
#include "Tests/Semantic/DebugInfoTest.h"
#include "Tests/Compiler/ProjectsTest.h"
#include "Tests/SourceManager/ProjectIncludeTest.h"

// The language server (C4LSPLib): JSON, protocol types, document sync, the session and every feature.
#include "Tests/Protocol/JsonReaderTest.h"
#include "Tests/Protocol/JsonValueTest.h"
#include "Tests/Protocol/JsonWriterTest.h"
#include "Tests/DAP/DapExceptionTest.h"
#include "Tests/DAP/DapExpressionTest.h"
#include "Tests/DAP/DapFormattingTest.h"
#include "Tests/DAP/DapInspectionTest.h"
#include "Tests/DAP/DapServerTest.h"
#include "Tests/DAP/DapSessionTest.h"
#include "Tests/DAP/DapStepTest.h"
#include "Tests/LSP/LspDefaultLibraryTest.h"
#include "Tests/LSP/LspFeaturesTest.h"
#include "Tests/LSP/LspFormattingTest.h"
#include "Tests/LSP/LspNewFeaturesTest.h"
#include "Tests/LSP/LspNavigationTest.h"
#include "Tests/LSP/LspSemanticTokensTest.h"
#include "Tests/LSP/LspServerTest.h"
#include "Tests/LSP/LspTidyTest.h"
#include "Tests/LSP/LspTypesTest.h"
#include "Tests/LSP/TextDocumentStoreTest.h"
#include "Tests/LSP/TextDocumentSyncTest.h"

// --filter, --benchmark-filter, --list-tests and --list-benchmarks, as CoreTesting and ReflectionTesting take them.
int main(int argc, char** argv)
{
	Core::Vector<Core::String> args;
	for (int i = 1; i < argc; i++)
		args.Push(argv[i]);
	Core::ArgList argList(args);
	if (Testing::ProcessCliArgs(argList))
		return 0;
	Testing::RunTestsAndBenchmarks();
	return Testing::GetExitCode();
}

