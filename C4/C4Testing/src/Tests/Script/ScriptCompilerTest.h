#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Compiling a script against its host's interface (Script/ScriptCompiler, docs/runtime/04 section 4): what the
	// module holds of the host and of the script, and what a script may not do.
	CLASS(GROUP = Testing) ScriptCompilerTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ScriptCompilerTest);

		TEST_FUNC(Script.Compiler)
		void AScriptCallsItsHost();
		TEST_FUNC(Script.Compiler)
		void HostGlobalsAreTheHostsAndNotStarted();
		TEST_FUNC(Script.Compiler)
		void HostTemplateInstancesAreTheHosts();
		TEST_FUNC(Script.Compiler)
		void AScriptDerivesFromHostClasses();
		TEST_FUNC(Script.Compiler)
		void TypeDescriptorsAreTheScriptsOwn();
		TEST_FUNC(Script.Compiler)
		void ObjectsOfHostClassesUseTheHostsVtables();
		TEST_FUNC(Script.Compiler)
		void ExitsAndThrowsGoThroughTheScriptRuntime();
		TEST_FUNC(Script.Compiler)
		void HostThreadLocalsGoThroughTheirAccessor();
		TEST_FUNC(Script.Compiler)
		void HostThreadLocalsInACatchNameTheirFunclet();
		TEST_FUNC(Script.Compiler)
		void AThreadLocalThatRunsCodeStartsOnFirstUse();
		TEST_FUNC(Script.Compiler)
		void AFirstUseStartThrowsToTheEnclosingHandler();
		TEST_FUNC(Script.Compiler)
		void ReflectionHoldsOnlyTheScriptsOwn();
		TEST_FUNC(Script.Compiler)
		void HostBodiesAreInlined();
		TEST_FUNC(Script.Compiler)
		void ImportingCOrCppIsAnError();
		TEST_FUNC(Script.Compiler)
		void AnIncludeOffTheAllowListIsAnError();
		TEST_FUNC(Script.Compiler)
		void DefiningAHostFunctionIsAnError();
		TEST_FUNC(Script.Compiler)
		void ASyntaxErrorGivesDiagnosticsAndNoModule();
		TEST_FUNC(Script.Compiler)
		void AMissingFileIsAnError();
	};
} // namespace Testing
