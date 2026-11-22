#include "DeclarationResolutionTest.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ConstEvaluator.h"
#include "Model/ProgramModel.h"
#include "Model/TemplateInstantiator.h"
#include "Model/TypeHandle.h"
#include <string>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		// One program's model: its types, globals and constants by name, and what the build reported.
		class Program
		{
		public:
			explicit Program(const std::string& source)
			{
				m_Compiler.RegisterInMemory("decls.cf", source.c_str());
				m_Compiler.CompileAll();
				m_Compiler.AnalyzeProgram();
			}

			const ProgramModel& Model() { return m_Compiler.GetProgramModel(); }
			const Type* TypeNamed(const char* name)
			{
				auto type = Model().QueryType(name);
				return type.Ok() ? type.value : nullptr;
			}
			const ObjectType* Object(const char* name) { return dynamic_cast<const ObjectType*>(TypeNamed(name)); }
			int64 SizeOf(const char* name)
			{
				const Type* type = TypeNamed(name);
				return type ? static_cast<int64>(type->GetSize()) : -1;
			}
			// The element count of the global array `name`.
			int64 Extent(const char* name)
			{
				auto variable = Model().QueryVariable(name);
				const auto* array = variable.Ok() ? dynamic_cast<const ArrayType*>(TypeHandle::UnwrapToValue(variable.value.type)) : nullptr;
				return array ? static_cast<int64>(array->GetElementCount()) : -1;
			}
			int64 Constant(const char* name)
			{
				const QueryResult<ConstantValue> value = ConstEvaluator(Model()).EvaluateName(name);
				return value.Ok() ? value.value.intValue : INT64_MIN;
			}
			std::string Diagnostics()
			{
				std::string all;
				for (const auto& e : m_Compiler.GetAllDiagnostics()) all += std::string(e.message.c_str()) + "\n";
				return all;
			}
			bool Reports(const char* part) { return Diagnostics().find(part) != std::string::npos; }
			uint32 TimesReported(const char* part)
			{
				const std::string all = Diagnostics();
				uint32 count = 0;
				for (size_t at = all.find(part); at != std::string::npos; at = all.find(part, at + 1)) ++count;
				return count;
			}

		private:
			CompilerInstance m_Compiler;
		};

		const char* const c_Main = "int32 main() { return 0; }\n";
	}

	// Every declaration names something declared after it; the old rounds needed several passes for these.
	void DeclarationResolutionTest::ADeclarationResolvesWhatItNamesFirst()
	{
		Program program(std::string(
			"struct Late { int32[kCount] items; Shape::Kind kind; }\n"
			"const int32 kCount = Sizes::Big / 2;\n"
			"struct Sizes { static const int32 Big = sizeof(Pair) * 2; }\n"
			"struct Pair { Word a; Word b; }\n"
			"alias Word = Wide;\n"
			"alias Wide = uint16;\n"
			"struct Shape { enum Kind : uint8 { Round, Square = kCount } }\n"
			"enum Flags { First = sizeof(Late), Second = First + offsetof(Late, kind) }\n"
			"template<T> struct Holder { T value; }\n"
			"int32[sizeof(Holder<Pair>)] g_Sized;\n"
			"struct Derived : Holder<Late> { int32[sizeof(Pair)] tail; }\n") + c_Main);
		TEST_ASSERT_EQ(program.Diagnostics(), std::string());
		TEST_ASSERT_EQ(program.Constant("kCount"), int64(4));
		TEST_ASSERT_EQ(program.SizeOf("Pair"), int64(4));
		TEST_ASSERT_EQ(program.SizeOf("Late"), int64(20));
		TEST_ASSERT_EQ(program.Constant("Shape::Square"), int64(4));
		TEST_ASSERT_EQ(program.Constant("First"), int64(20));
		TEST_ASSERT_EQ(program.Constant("Second"), int64(36));
		TEST_ASSERT_EQ(program.Extent("g_Sized"), int64(4));
		TEST_ASSERT_EQ(program.SizeOf("Derived"), int64(36));
		TEST_PASSED();
	}

	// BUG-480: `items` resolves through three other declarations, `kind` at once; `items` is still the first field.
	void DeclarationResolutionTest::FieldsKeepTheirDeclarationOrder()
	{
		Program program(std::string(
			"struct Late { int32[kCount] items; uint8 kind; int32[kCount] more; uint8 last; }\n"
			"const int32 kCount = Sizes::Big / 2;\n"
			"struct Sizes { static const int32 Big = sizeof(Pair) * 2; }\n"
			"struct Pair { uint16 a; uint16 b; }\n") + c_Main);
		const ObjectType* late = program.Object("Late");
		TEST_ASSERT(late != nullptr);
		TEST_ASSERT_EQ(late->GetFields().Count(), 4u);
		TEST_ASSERT_EQ(std::string(late->GetFields()[0].mName.c_str()), std::string("items"));
		TEST_ASSERT_EQ(std::string(late->GetFields()[1].mName.c_str()), std::string("kind"));
		TEST_ASSERT_EQ(std::string(late->GetFields()[2].mName.c_str()), std::string("more"));
		TEST_ASSERT_EQ(std::string(late->GetFields()[3].mName.c_str()), std::string("last"));
		TEST_ASSERT_EQ(static_cast<int64>(late->GetFieldOffset("kind")), int64(16));
		TEST_ASSERT_EQ(static_cast<int64>(late->GetFieldOffset("more")), int64(20));
		TEST_PASSED();
	}

	// BUG-478: the variable was resolved before the enum, whose shell measured as int32.
	void DeclarationResolutionTest::AnEnumIsMeasuredWithItsUnderlyingType()
	{
		Program program(std::string(
			"int32[sizeof(Small)] g_BySize;\n"
			"int32[sizeof(Wide) / 2] g_ByWideSize;\n"
			"const Small kLast = (Small)300;\n"
			"enum Small : uint8 { A, B }\n"
			"enum Wide : int64 { C }\n") + c_Main);
		TEST_ASSERT_EQ(program.Extent("g_BySize"), int64(1));
		TEST_ASSERT_EQ(program.Extent("g_ByWideSize"), int64(4));
		TEST_ASSERT_EQ(program.Constant("kLast"), int64(44)); // 300 in a uint8
		TEST_PASSED();
	}

	// BUG-478: the union was laid out while its struct member was a shell.
	void DeclarationResolutionTest::AUnionIsMeasuredWithItsMembers()
	{
		Program program(std::string(
			"struct User { int32[sizeof(Either)] tail; }\n"
			"union Either { Big big; uint8 byte; }\n"
			"struct Big { int64 a; int64 b; int64 c; }\n") + c_Main);
		TEST_ASSERT_EQ(program.SizeOf("Either"), int64(24));
		TEST_ASSERT_EQ(program.SizeOf("User"), int64(96));
		TEST_PASSED();
	}

	void DeclarationResolutionTest::AStaticMemberIsFoundBeforeItsClassResolves()
	{
		Program program(std::string(
			"int32[Grid::kCells] g_Cells;\n"
			"struct Grid { int32[kCells] cells; static const int32 kCells = kSide * kSide; static const int32 kSide = 3; }\n") + c_Main);
		TEST_ASSERT_EQ(program.Diagnostics(), std::string());
		TEST_ASSERT_EQ(program.Extent("g_Cells"), int64(9));
		TEST_ASSERT_EQ(program.SizeOf("Grid"), int64(36));
		TEST_PASSED();
	}

	// A field written before the static it names found a global of the same name while the static was unregistered.
	void DeclarationResolutionTest::AStaticMemberHidesAGlobalOfItsName()
	{
		Program program(std::string(
			"const int32 kCount = 2;\n"
			"struct Rack { int32[kCount] slots; static const int32 kCount = 5; }\n") + c_Main);
		TEST_ASSERT_EQ(program.SizeOf("Rack"), int64(20));
		TEST_PASSED();
	}

	// Each alias names the next one, declared after it: one round resolved one alias, and 64 rounds were the cap.
	void DeclarationResolutionTest::ALongChainResolvesInOneSweep()
	{
		constexpr int c_Links = 150;
		std::string source;
		for (int i = 0; i < c_Links; ++i) source += "alias Link" + std::to_string(i) + " = Link" + std::to_string(i + 1) + ";\n";
		source += "alias Link" + std::to_string(c_Links) + " = int16;\nLink0 g_First;\n";
		Program program(source + c_Main);
		TEST_ASSERT_EQ(program.Diagnostics(), std::string());
		TEST_ASSERT_EQ(program.SizeOf("Link0"), int64(2));
		TEST_PASSED();
	}

	// `First`'s signature needs an alias chain written backwards; it is still the first record of its class.
	void DeclarationResolutionTest::MethodRecordsFollowDeclarationOrder()
	{
		Program program(std::string(
			"struct Api { Outer First(); int32 Second(); Outer Third(); }\n"
			"alias Outer = Middle;\n"
			"alias Middle = Inner;\n"
			"alias Inner = int64;\n") + c_Main);
		Core::Vector<const FunctionRecord*> records;
		program.Model().AllFunctionRecords(records);
		std::string order;
		for (const FunctionRecord* record : records)
			if (record->containingType && record->containingType->GetName() == "Api") order += std::string(record->name.c_str()) + " ";
		TEST_ASSERT_EQ(order, std::string("First Second Third "));
		TEST_PASSED();
	}

	void DeclarationResolutionTest::AnAliasCycleNamesItsCycle()
	{
		Program program(std::string("alias CA = CB;\nalias CB = CC;\nalias CC = CA;\nCA g_Value;\n") + c_Main);
		TEST_ASSERT(program.Reports("unresolved alias target for 'CA': the alias 'CA' depends on itself: CA -> CB -> CC -> CA"));
		TEST_ASSERT(program.Reports("unresolved alias target for 'CB': the alias 'CA' depends on itself: CA -> CB -> CC -> CA"));
		TEST_ASSERT(program.Reports("unresolved type for variable 'g_Value': the alias 'CA' depends on itself"));
		TEST_PASSED();
	}

	void DeclarationResolutionTest::ALayoutCycleNamesItsCycle()
	{
		Program program(std::string(
			"struct SA { int32[sizeof(SB)] x; }\n"
			"struct SB { int32[sizeof(SA)] y; }\n"
			"struct Self { int32[sizeof(Self)] pad; }\n"
			"struct Fine { int32[sizeof(Fine*)] ok; }\n") + c_Main);
		// The chain names the field each layout waits for.
		TEST_ASSERT(program.Reports("unresolved type for field 'y' in 'SB': the array size 'sizeof(SA)' is not a constant expression: the layout of 'SA' "
			"depends on itself: SA -> SA::x -> SB -> SB::y -> SA"));
		TEST_ASSERT(program.Reports("unresolved type for field 'x' in 'SA': the array size 'sizeof(SB)' is not a constant expression: the layout of 'SA' "
			"depends on itself: SA -> SA::x -> SB -> SB::y -> SA"));
		TEST_ASSERT(program.Reports("the layout of 'Self' depends on itself: Self -> Self::pad -> Self"));
		TEST_ASSERT(!program.Reports("'Fine'"));
		TEST_ASSERT_EQ(program.SizeOf("Fine"), int64(32));
		TEST_PASSED();
	}

	void DeclarationResolutionTest::AnEnumCycleNamesItsCycle()
	{
		Program program(std::string("enum EA { X = EB::Y }\nenum EB { Y = EA::X }\n") + c_Main);
		TEST_ASSERT(program.Reports("unresolved enum 'EB': member 'Y': the enum 'EA' depends on itself: EA -> EB -> EA"));
		TEST_ASSERT(program.Reports("unresolved enum 'EA': member 'X': the enum 'EA' depends on itself: EA -> EB -> EA"));
		TEST_PASSED();
	}

	void DeclarationResolutionTest::ACycleThroughAConstantNamesIt()
	{
		Program program(std::string("int32[kN] g_Arr;\nconst int32 kN = sizeof(g_Arr);\n") + c_Main);
		TEST_ASSERT(program.Reports("unresolved type for variable 'g_Arr': the array size 'kN' is not a constant expression"));
		TEST_ASSERT(program.Reports("the type of 'g_Arr' depends on itself: g_Arr -> kN -> g_Arr"));
		TEST_PASSED();
	}

	void DeclarationResolutionTest::AFailedDeclarationIsReportedOnceAndItsUsersSayWhich()
	{
		Program program(std::string(
			"alias Handle = Missing;\n"
			"struct Owner { Handle first; Handle second; int32 fine; }\n"
			"Handle g_Handle;\n"
			"enum Level { Low = Unknown }\n"
			"const int32 kTwice = Level::Low * 2;\n"
			"int32[kTwice] g_Levels;\n") + c_Main);
		TEST_ASSERT_EQ(program.TimesReported("unresolved alias target for 'Handle'"), 1u);
		TEST_ASSERT(program.Reports("unresolved alias target for 'Handle': type not found: Missing"));
		TEST_ASSERT(program.Reports("unresolved type for field 'first' in 'Owner': the alias 'Handle' does not resolve"));
		TEST_ASSERT(program.Reports("unresolved type for field 'second' in 'Owner': the alias 'Handle' does not resolve"));
		TEST_ASSERT(program.Reports("unresolved type for variable 'g_Handle': the alias 'Handle' does not resolve"));
		TEST_ASSERT_EQ(program.TimesReported("unresolved enum 'Level'"), 1u);
		TEST_ASSERT(program.Reports("unresolved type for variable 'g_Levels': the array size 'kTwice' is not a constant expression"));
		TEST_ASSERT(program.Reports("the enum 'Level' does not resolve"));
		// The field that does resolve is still a field of its object.
		const ObjectType* owner = program.Object("Owner");
		TEST_ASSERT(owner != nullptr && owner->GetFieldByName("fine") != nullptr);
		TEST_PASSED();
	}

	// A lane view whose lane type did not resolve was dropped without a word.
	void DeclarationResolutionTest::AnUnresolvedLaneViewIsReported()
	{
		Program program(std::string("struct Vec { public union { packed Missing x, y; }; }\n") + c_Main);
		TEST_ASSERT(program.Reports("unresolved lane type for a lane view in 'Vec': type not found: Missing"));
		TEST_PASSED();
	}

	// BUG-479: each instance's field needed the next, larger one until the compiler's stack ran out.
	void DeclarationResolutionTest::ATemplateWithoutALastInstanceIsAnError()
	{
		Program program(std::string("template<T> struct W { W<W<T>>* next; }\nW<int32> g_W;\n") + c_Main);
		const std::string limit = std::to_string(TemplateInstantiator::c_MaxInstantiationDepth);
		TEST_ASSERT(program.Reports(("instantiating 'W' goes more than " + limit + " instances deep").c_str()));
		TEST_ASSERT_EQ(program.TimesReported("instances deep"), 1u);
		TEST_ASSERT_EQ(program.TimesReported("cannot instantiate"), 0u); // the chain is not wrapped once per instance
		TEST_PASSED();
	}

	// Each template's instance holds the next template's, so the first is built with all the others inside it.
	void DeclarationResolutionTest::ADeepButEndingInstantiationWorks()
	{
		const int depth = static_cast<int>(TemplateInstantiator::c_MaxInstantiationDepth) - 5;
		std::string source;
		for (int i = 0; i + 1 < depth; ++i)
			source += "template<T> struct Chain" + std::to_string(i) + " { Chain" + std::to_string(i + 1) + "<T> rest; T one; }\n";
		source += "template<T> struct Chain" + std::to_string(depth - 1) + " { T one; }\nChain0<uint8> g_Chain;\n";
		Program program(source + c_Main);
		TEST_ASSERT_EQ(program.Diagnostics(), std::string());
		auto chained = program.Model().QueryVariable("g_Chain");
		TEST_ASSERT(chained.Ok() && chained.value.type != nullptr);
		TEST_ASSERT_EQ(static_cast<int64>(chained.value.type->GetSize()), int64(depth));
		TEST_PASSED();
	}

	// Each instance's method names the next template in its body: the walk opens the bodies it makes, with no
	// cap on how many links that takes (it was eight).
	void DeclarationResolutionTest::BodiesOfInstancesAreOpenedHoweverDeepTheChain()
	{
		constexpr int c_Links = 20;
		std::string source;
		for (int i = 0; i < c_Links; ++i)
			source += "template<T> struct Link" + std::to_string(i) + " { void Next() { Link" + std::to_string(i + 1) + "<int32> next; } T held; }\n";
		source += "template<T> struct Link" + std::to_string(c_Links) + " { T held; }\n";
		source += "int32 main() { Link0<int32> first; return 0; }\n";
		Program program(source);
		TEST_ASSERT_EQ(program.Diagnostics(), std::string());
		bool lastMade = false;
		const std::string last = "Link" + std::to_string(c_Links);
		for (const ObjectType* type : program.Model().ObjectTypes())
			lastMade = lastMade || (std::string(type->GetName().c_str()) == last && !type->GetTemplateArguments().Empty());
		TEST_ASSERT(lastMade);
		TEST_PASSED();
	}

	// A global's instance is built when the global resolves, before anything folded the constants its template names:
	// they fold then, looked up from the template's own namespace or class (a constant written after it too).
	void DeclarationResolutionTest::AnInstancesExtentFoldsAConstantOfItsTemplatesScope()
	{
		Program program(std::string(
			"namespace A {\n"
			"\tnamespace Detail { const int32 kRows = 2; }\n"
			"\tenum Size { Small = 2, Big = 5 }\n"
			"\ttemplate<T> struct Box { T[kCount] slots; }\n"
			"\ttemplate<T> struct Grid { T[Detail::kRows] rows; T[Big] wide; }\n"
			"\tstruct Limits { static const int32 kMax = 4; template<T> struct Row { T[kMax] cells; } }\n"
			"\tconst int32 kCount = 3;\n"
			"}\n"
			"A::Box<int32> g_Box;\nA::Grid<int32> g_Grid;\nA::Limits::Row<int32> g_Row;\n") + c_Main);
		TEST_ASSERT_EQ(program.Diagnostics(), std::string());
		auto sizeOfGlobal = [&program](const char* name) {
			auto variable = program.Model().QueryVariable(name);
			return variable.Ok() && variable.value.type ? static_cast<int64>(variable.value.type->GetSize()) : int64(-1);
		};
		TEST_ASSERT_EQ(sizeOfGlobal("g_Box"), int64(12));
		TEST_ASSERT_EQ(sizeOfGlobal("g_Grid"), int64(28));
		TEST_ASSERT_EQ(sizeOfGlobal("g_Row"), int64(16));
		TEST_PASSED();
	}

	// BUG-481: a static constant named through a template instance folds in the instance - qualified, inherited, in a
	// global's extent, a field's and a constant's - and a static that is not const says so.
	void DeclarationResolutionTest::AnInstancesStaticConstantFolds()
	{
		Program program(std::string(
			"namespace Ns { template<T> struct Holder { public T value; public static const int32 Size = sizeof(T); } }\n"
			"template<T> struct Derived : Ns::Holder<T> { public int32 extra; }\n"
			"struct Pair { public uint16 a; public uint16 b; }\n"
			"struct Table { public int32[Ns::Holder<int64>::Size] cells; }\n"
			"int32[Ns::Holder<Pair>::Size] g_Global;\n"
			"int32[Derived<int16>::Size] g_Inherited;\n"
			"const int32 kTwice = Ns::Holder<Pair>::Size * 2;\n") + c_Main);
		TEST_ASSERT_EQ(program.Diagnostics(), std::string());
		TEST_ASSERT_EQ(program.Extent("g_Global"), int64(4));
		TEST_ASSERT_EQ(program.Extent("g_Inherited"), int64(2));
		TEST_ASSERT_EQ(program.SizeOf("Table"), int64(32));
		TEST_ASSERT_EQ(program.Constant("kTwice"), int64(8));
		Program variable(std::string(
			"template<T> struct Counter { public static int32 s_count; }\n"
			"int32[Counter<int32>::s_count] g_Bad;\n") + c_Main);
		TEST_ASSERT_MSG(variable.Reports("'Counter<int32>::s_count' is a static variable: only a const with a constant initializer folds"),
			variable.Diagnostics().c_str());
		TEST_PASSED();
	}
} // namespace Testing
