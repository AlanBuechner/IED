#include "BuiltinAliasTest.h"
#include "Model/TypeHandle.h"

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		ProgramModel& MakeModel(CompilerInstance& compiler, const char* source)
		{
			compiler.RegisterInMemory("t.cf", source);
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			return compiler.GetProgramModelForLowering();
		}

		const Type* VariableType(const ProgramModel& model, const char* name)
		{
			auto v = model.QueryVariable(name);
			return v.Ok() ? TypeHandle::UnwrapToValue(v.value.type) : nullptr;
		}
	}

	void BuiltinAliasTest::AliasesResolveToTheirTargets()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, "int32 main() { return 0; }\n");
		const struct { const char* alias; const char* target; } pairs[] = {
			{ "int", "int32" }, { "uint", "uint32" }, { "char", "char8" } };
		for (const auto& pair : pairs)
		{
			auto alias = model.QueryType(pair.alias);
			auto target = model.QueryType(pair.target);
			TEST_ASSERT(alias.Ok() && target.Ok());
			TEST_ASSERT(alias.value == target.value);
			TEST_ASSERT(alias.value->IsPrimitive());
			TEST_ASSERT(alias.value->GetName() == pair.target);
		}
	}

	void BuiltinAliasTest::AliasedVariablesShareOneType()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler,
			"int a; int32 b; uint c; uint32 d; char e; char8 f; int* p; int32* q;\nint32 main() { return 0; }\n");
		TEST_ASSERT(VariableType(model, "a") && VariableType(model, "a") == VariableType(model, "b"));
		TEST_ASSERT(VariableType(model, "c") && VariableType(model, "c") == VariableType(model, "d"));
		TEST_ASSERT(VariableType(model, "e") && VariableType(model, "e") == VariableType(model, "f"));
		TEST_ASSERT(VariableType(model, "p") && VariableType(model, "p") == VariableType(model, "q"));
	}

	void BuiltinAliasTest::UserTypesShadowNoAlias()
	{
		// An alias is not a keyword: `loop`, `int8` and the aliases are names, and a user struct named
		// `Int` (a different name) is unaffected.
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, "struct Int { public int value; }\nInt x;\nint32 main() { return 0; }\n");
		const Type* x = VariableType(model, "x");
		TEST_ASSERT(x && !x->IsPrimitive() && x->GetName() == "Int");
	}
} // namespace Testing
