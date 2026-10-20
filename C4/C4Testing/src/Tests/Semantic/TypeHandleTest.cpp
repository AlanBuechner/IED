#include "TypeHandleTest.h"
#include "Model/TypeHandle.h"

namespace Testing
{
	using namespace Scripting;

	static ProgramModel& MakeModel(CompilerInstance& compiler, const char* source)
	{
		compiler.RegisterInMemory("t.cf", source);
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		return compiler.GetProgramModelForLowering();
	}

	static bool HoldsPointers(const ProgramModel& model, const char* typeName)
	{
		auto t = model.QueryType(typeName);
		return t.Ok() && TypeHandle::MayHoldPointers(t.value);
	}

	void TypeHandleTest::ScalarsEnumsAndFunctionPointersHoldNoPointers()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"enum class Mode { A, B }\n"
			"struct Floats { float a; float[4] b; int64 c; Mode m; }\n"
			"struct Nested { Floats inner; uint8[16] bytes; }\n"
			"struct Callback { funcptr<int32(int32)> fn; int32 arg; }\n"
			"union Bits { float f; uint32 u; }\n"
			"alias Grid = Floats[3];\n");
		for (const char* name : { "int32", "float", "Mode", "Floats", "Nested", "Callback", "Bits", "Grid" })
			TEST_ASSERT(!HoldsPointers(model, name));
	}

	void TypeHandleTest::PointersAndReferencesAnywhereInATypeHoldPointers()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Node { int32 v; Node* next; }\n"
			"struct Base { void* data; }\n"
			"struct Derived : Base { int32 extra; }\n"
			"struct InArray { int32 n; Node*[4] nodes; }\n"
			"struct Deep { InArray[2] rows; }\n"
			"union Either { float f; Node* node; }\n"
			"struct WithUnion { Either e; }\n"
			"alias NodePtr = Node*;\n");
		for (const char* name : { "Node", "Derived", "InArray", "Deep", "Either", "WithUnion", "NodePtr" })
			TEST_ASSERT(HoldsPointers(model, name));
		TEST_ASSERT(TypeHandle::MayHoldPointers(nullptr)); // unknown: assume it does
	}

	void TypeHandleTest::StaticFieldsDoNotCount()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "struct Counter { int32 count; public static Counter* s_instance; }\n");
		TEST_ASSERT(!HoldsPointers(model, "Counter"));
	}
} // namespace Testing
