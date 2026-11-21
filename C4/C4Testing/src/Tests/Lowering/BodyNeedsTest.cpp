#include "BodyNeedsTest.h"
#include "Backend/HIR/HIR.h"
#include "Backend/Lowering/BodyNeeds.h"
#include "Backend/Lowering/ProgramLowering.h"
#include "Backend/Lowering/VtableEntries.h"
#include "Compiler/CompilerInstance.h"
#include "Model/Types/QualifiedName.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		ProgramModel& MakeModel(CompilerInstance& compiler, const std::string& source)
		{
			compiler.RegisterInMemory("t.cf", source.c_str());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			return compiler.GetProgramModelForLowering();
		}

		std::string NameOf(const FunctionRecord& record)
		{
			const std::string name = record.name.empty() ? record.mangledName.c_str() : record.name.c_str();
			return record.containingType ? std::string(QualifiedName(record.containingType).c_str()) + "::" + name : name;
		}

		const ObjectType* TypeNamed(const ProgramModel& model, const std::string& name)
		{
			for (const ObjectType* type : model.ObjectTypes())
				if (QualifiedName(type).c_str() == name) return type;
			return nullptr;
		}

		std::vector<std::string> Names(const std::vector<const FunctionRecord*>& records)
		{
			std::vector<std::string> names;
			for (const FunctionRecord* record : records) names.push_back(NameOf(*record));
			return names;
		}

		bool Has(const std::vector<std::string>& names, const std::string& name)
		{
			return std::find(names.begin(), names.end(), name) != names.end();
		}

		// The needs of `function`'s body, lowered with the program as ProgramLowering lowers it; its tree is kept in `tree`.
		BodyNeeds NeedsOfFunction(ProgramModel& model, const std::string& function, std::unique_ptr<HIRFunction>& tree)
		{
			ProgramLowering lowering(model, {});
			const bool lowered = lowering.Run();
			for (const CompilationError& error : lowering.Errors().GetErrors()) std::printf("%s\n", error.message.c_str());
			TEST_ASSERT(lowered);
			for (LoweredUnit& unit : lowering.Program().units)
				if (unit.tree->record && NameOf(*unit.tree->record) == function) tree = std::move(unit.tree);
			TEST_ASSERT(tree != nullptr);
			return tree ? NeedsOf(*tree) : BodyNeeds();
		}

		constexpr const char* kTracked =
			"class Tracked\n"
			"{\n"
			"\tpublic int32 n;\n"
			"\t[operator(\"create\")] public void Init() { n = 1; }\n"
			"\t[operator(\"copy\")] public void CopyFrom(const Tracked& other) { n = other.n; }\n"
			"\t[operator(\"delete\")] public void Destroy() { n = 0; }\n"
			"}\n";
	}

	void BodyNeedsTest::CallsConstructionsAndAddressesAreNeeded()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, std::string(kTracked) +
			"int32 Twice(int32 x) { return x * 2; }\n"
			"int32 Use() { Tracked t; funcptr<int32(int32)> f = Twice; return f(t.n) + Twice(1); }\n"
			"int32 main() { return Use(); }\n");
		std::unique_ptr<HIRFunction> tree;
		const std::vector<std::string> needs = Names(NeedsOfFunction(model, "Use", tree).functions);
		TEST_ASSERT(Has(needs, "Twice"));
		TEST_ASSERT(Has(needs, "Tracked::Init"));
		TEST_ASSERT(Has(needs, "Tracked::Destroy")); // the local's destruction at scope exit
	}

	// LastUseMoves turns the copy into a move: the copy's call stays in the arena, out of the tree codegen emits.
	void BodyNeedsTest::AMovedCopyIsNotNeeded()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, std::string(kTracked) +
			"int32 Take(Tracked t) { return t.n; }\n"
			"int32 Use() { Tracked t; return Take(t); }\n"
			"int32 main() { return Use(); }\n");
		std::unique_ptr<HIRFunction> tree;
		const std::vector<std::string> needs = Names(NeedsOfFunction(model, "Use", tree).functions);
		TEST_ASSERT(Has(needs, "Take"));
		TEST_ASSERT(!Has(needs, "Tracked::CopyFrom"));
		bool madeCopy = false;
		for (const HIRNode* node : tree->m_Nodes)
			if (node->kind == HIRNode::Kind::Call && static_cast<const CallNode*>(node)->callee &&
				NameOf(*static_cast<const CallNode*>(node)->callee) == "Tracked::CopyFrom")
				madeCopy = true;
		TEST_ASSERT(madeCopy);
	}

	void BodyNeedsTest::AThrowNeedsItsDestroyAndCopy()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, std::string(kTracked) +
			"void Fail() { Tracked t; throw t; }\n"
			"int32 main() { try { Fail(); } catch (Tracked caught) { return caught.n; } return 0; }\n");
		std::unique_ptr<HIRFunction> tree;
		const std::vector<std::string> needs = Names(NeedsOfFunction(model, "Fail", tree).functions);
		TEST_ASSERT(Has(needs, "Tracked::Destroy"));
		TEST_ASSERT(Has(needs, "Tracked::CopyFrom"));
	}

	void BodyNeedsTest::AnAllocationNeedsItsCensusCounterAndFinalizer()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, std::string(kTracked) +
			"class Buffer { public int64 size = 16; [operator(\"native_bytes\")] public int64 NativeBytes() const { return size; } }\n"
			"int32 Use() { Tracked* t = new Tracked(); Buffer* b = new Buffer(); return t->n + (int32)b->size; }\n"
			"int32 main() { return Use(); }\n");
		std::unique_ptr<HIRFunction> tree;
		const BodyNeeds needs = NeedsOfFunction(model, "Use", tree);
		TEST_ASSERT(Has(Names(needs.functions), "Tracked::Destroy")); // the GC finalizer
		const ObjectType* buffer = TypeNamed(model, "Buffer");
		TEST_ASSERT(buffer != nullptr);
		TEST_ASSERT(std::any_of(needs.census.begin(), needs.census.end(), [&](const CensusNeed& need) { return need == CensusNeed{ buffer, 0 }; }));
		for (const CensusNeed& need : needs.census) TEST_ASSERT(need.type == buffer); // Tracked holds no native memory
	}

	void BodyNeedsTest::ALocalsObjectIsHeld()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler,
			"class Base { public virtual int32 Kind() { return 1; } }\n"
			"int32 Use(Base* p) { Base local; return local.Kind() + (p == null ? 0 : 1); }\n"
			"int32 main() { return Use(null); }\n");
		std::unique_ptr<HIRFunction> tree;
		const BodyNeeds needs = NeedsOfFunction(model, "Use", tree);
		const ObjectType* base = TypeNamed(model, "Base");
		TEST_ASSERT(std::find(needs.objects.begin(), needs.objects.end(), base) != needs.objects.end());
	}

	void BodyNeedsTest::AnObjectsVtablesHoldItsFinalOverriders()
	{
		CompilerInstance compiler;
		const ProgramModel& model = MakeModel(compiler,
			"interface INamed { int32 Id(); }\n"
			"class Base { public virtual int32 Kind() { return 1; } public virtual int32 Size() { return 4; } }\n"
			"class Derived : Base, INamed { public override int32 Kind() { return 2; } public int32 Id() { return 3; } }\n"
			"int32 main() { return 0; }\n");
		VtableEntries entries(model);
		const std::vector<std::string> names = Names(entries.Of(TypeNamed(model, "Derived")));
		TEST_ASSERT(Has(names, "Derived::Kind"));
		TEST_ASSERT(Has(names, "Base::Size"));
		TEST_ASSERT(Has(names, "Derived::Id"));
		TEST_ASSERT(!Has(names, "Base::Kind")); // overridden
	}

	void BodyNeedsTest::FieldsAndElementsBringTheirVtables()
	{
		CompilerInstance compiler;
		const ProgramModel& model = MakeModel(compiler,
			"class Base { public virtual int32 Kind() { return 1; } }\n"
			"class Other { public virtual int32 Weight() { return 5; } }\n"
			"struct Holder { public Base one; public Other[2] pair; public Base* pointed; }\n"
			"int32 main() { return 0; }\n");
		VtableEntries entries(model);
		const std::vector<std::string> names = Names(entries.Of(TypeNamed(model, "Holder")));
		TEST_ASSERT(Has(names, "Base::Kind"));
		TEST_ASSERT(Has(names, "Other::Weight"));
		TEST_ASSERT_EQ(names.size(), size_t(2));
	}

	void BodyNeedsTest::APointerOrANumberHoldsNoVtable()
	{
		CompilerInstance compiler;
		ProgramModel& model = MakeModel(compiler, "class Base { public virtual int32 Kind() { return 1; } }\nint32 main() { return 0; }\n");
		VtableEntries entries(model);
		const ObjectType* base = TypeNamed(model, "Base");
		TEST_ASSERT(entries.Of(model.TypeStorePtr()->Pointer(base)).empty());
		TEST_ASSERT(entries.Of(model.TypeStorePtr()->Builtin(TypeKind::Int32)).empty());
		TEST_ASSERT_EQ(entries.Of(base).size(), size_t(1));
	}
} // namespace Testing
