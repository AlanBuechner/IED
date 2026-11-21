#include "CompileTimeTest.h"
#include "Backend/HIR/GlobalInit.h"
#include "Backend/HIR/LoweringDriver.h"
#include "Backend/MIR/CompileTimeCheck.h"
#include "Backend/MIR/CompileTimeEvaluator.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/ErrorTracker.h"
#include "Model/Annotations.h"
#include "Model/ProgramModel.h"
#include "Model/Types/ObjectType.h"
#include <cstring>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		class Program
		{
		public:
			explicit Program(const std::string& source)
			{
				m_Compiler.RegisterInMemory("t.cf", source.c_str());
				m_Compiler.CompileAll();
				m_Compiler.AnalyzeProgram();
				m_Model = &m_Compiler.GetProgramModelForLowering();
			}

			const FunctionRecord* Find(const char* name) const
			{
				Core::Vector<const FunctionRecord*> records;
				m_Model->AllFunctionRecords(records);
				for (const FunctionRecord* record : records)
					if (record->name == name && record->bodyNode) return record;
				return nullptr;
			}

			// What lowering `name` reports: empty when it compiles.
			std::string LoweringError(const char* name) const
			{
				const FunctionRecord* record = Find(name);
				if (!record) return std::string("no function ") + name;
				ErrorTracker errors;
				const HIRFunction fn = LowerFunction(*m_Model, errors, record);
				for (const CompilationError& error : errors.GetErrors())
					if (error.severity != ErrorSeverity::Warning) return std::string(error.message.c_str());
				return fn.complete ? std::string() : std::string("incomplete without an error");
			}

			ProgramModel& Model() const { return *m_Model; }

			bool Reported(const char* text) const
			{
				for (const CompilationError& error : m_Compiler.GetAllErrors())
					if (error.message.find(text) != Core::String::npos) return true;
				return false;
			}

		private:
			CompilerInstance m_Compiler;
			ProgramModel* m_Model = nullptr;
		};

		bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }
		ConstantValue Int(int64 value) { return ConstantValue::Int(value); }

		std::string MessageOf(const QueryResult<ConstantValue>& result) { return result.Ok() ? std::string() : std::string(result.message.c_str()); }
	}

	void CompileTimeTest::TheMarkIsOnAnyDeclaration()
	{
		Program p("[compile_time] int32 Declared(int32 x);\n"
			"int32 Declared(int32 x) { return x; }\n"
			"[compile_time] int32 Defined(int32 x) { return x; }\n"
			"int32 Plain(int32 x) { return x; }\n");
		TEST_ASSERT(p.Find("Declared") && IsCompileTime(*p.Find("Declared")));
		TEST_ASSERT(p.Find("Defined") && IsCompileTime(*p.Find("Defined")));
		TEST_ASSERT(p.Find("Plain") && !IsCompileTime(*p.Find("Plain")));
	}

	void CompileTimeTest::AMarkedFunctionThatCanRunCompiles()
	{
		Program p("struct V2 { public int32 x; public int32 y; [compile_time] public int32 Sum() const { return x + y; } }\n"
			"[compile_time] int32 Square(int32 x) { return x * x; }\n"
			"[compile_time] int32 SumOfSquares(int32 n) { int32 s = 0; for (int32 i = 0; i < n; ++i) s += Square(i); return s; }\n"
			"[compile_time] int32 Pair(int32 a) { V2 v = V2{ a, a * 2 }; return v.Sum(); }\n"
			"[compile_time] int32 Checked(int32 a) { if (a < 0) throw a; return a; }\n"
			"[compile_time] int32 Handled(int32 a) { try { return Checked(a); } catch (int32 e) { int32* p = new int32(); return *p + e; } }\n");
		TEST_ASSERT(p.LoweringError("Square").empty());
		TEST_ASSERT(p.LoweringError("SumOfSquares").empty());
		TEST_ASSERT(p.LoweringError("Pair").empty());
		TEST_ASSERT(p.LoweringError("Checked").empty());
		TEST_ASSERT(p.LoweringError("Handled").empty()); // a handler never runs at compile time: what it does is not checked
	}

	void CompileTimeTest::AMarkedFunctionMayDoOnlyWhatCompileTimeRuns()
	{
		Program p("int32 g = 3;\n"
			"int32 Plain(int32 x) { return x; }\n"
			"[compile_time] int32 Heap() { int32* q = new int32(); return *q; }\n"
			"[compile_time] int32 Global() { return g; }\n"
			"[compile_time] int32 CallsPlain(int32 x) { return Plain(x); }\n"
			"[compile_time] int32 Counter() { static int32 n = 0; n = n + 1; return n; }\n"
			"int32 Unmarked() { int32* q = new int32(); return *q; }\n");
		TEST_ASSERT(Contains(p.LoweringError("Heap"), "'Heap' is [compile_time], but it allocates on the heap ('new')"));
		TEST_ASSERT(Contains(p.LoweringError("Global"), "it uses the global variable 'g', which is not a constant"));
		TEST_ASSERT(Contains(p.LoweringError("CallsPlain"), "it calls 'Plain', which is not [compile_time]"));
		TEST_ASSERT(Contains(p.LoweringError("Counter"), "it has a static local"));
		TEST_ASSERT(p.LoweringError("Unmarked").empty());
	}

	void CompileTimeTest::TheMarkTakesNoArguments()
	{
		Program p("[compile_time(\"fast\")] int32 F(int32 x) { return x; }\n");
		TEST_ASSERT(Contains(p.LoweringError("F"), "[compile_time] takes no arguments"));
	}

	void CompileTimeTest::TheEvaluatorRunsAMarkedFunction()
	{
		Program p("[compile_time] int32 Square(int32 x) { return x * x; }\n"
			"[compile_time] int32 SumOfSquares(int32 n) { int32 s = 0; for (int32 i = 0; i < n; ++i) s += Square(i); return s; }\n"
			"[compile_time] bool IsPowerOfTwo(uint32 v) { return v != 0 && (v & (v - 1)) == 0; }\n"
			"[compile_time] double Half(double v) { return v / 2.0; }\n");
		CompileTimeEvaluator evaluator(p.Model());
		const QueryResult<ConstantValue> sum = evaluator.Call(*p.Find("SumOfSquares"), { Int(4) }, SourceLocation());
		TEST_ASSERT(sum.Ok() && sum.value.AsInt() == 14);
		const QueryResult<ConstantValue> power = evaluator.Call(*p.Find("IsPowerOfTwo"), { Int(64) }, SourceLocation());
		TEST_ASSERT(power.Ok() && power.value.kind == ConstantValue::Kind::Bool && power.value.booleanValue);
		const QueryResult<ConstantValue> half = evaluator.Call(*p.Find("Half"), { Int(5) }, SourceLocation()); // an int argument made a double
		TEST_ASSERT(half.Ok() && half.value.floatValue == 2.5);
		const QueryResult<ConstantValue> again = evaluator.Call(*p.Find("SumOfSquares"), { Int(10) }, SourceLocation()); // bodies are kept
		TEST_ASSERT(again.Ok() && again.value.AsInt() == 285);
	}

	void CompileTimeTest::TheEvaluatorRefusesWhatItCannotRun()
	{
		Program p("struct V2 { public int32 x; public int32 y; }\n"
			"int32 Plain(int32 x) { return x; }\n"
			"[compile_time] int32 CallsPlain(int32 x) { return Plain(x); }\n"
			"[compile_time] V2 Both(int32 a) { return V2{ a, a }; }\n"
			"[compile_time] int32 Divide(int32 a) { return 100 / a; }\n");
		CompileTimeEvaluator evaluator(p.Model());
		TEST_ASSERT(Contains(MessageOf(evaluator.Call(*p.Find("Plain"), { Int(1) }, SourceLocation())), "'Plain' is not [compile_time]"));
		TEST_ASSERT(Contains(MessageOf(evaluator.Call(*p.Find("CallsPlain"), { Int(1) }, SourceLocation())), "'CallsPlain' cannot run at compile time: it does not compile"));
		TEST_ASSERT(Contains(MessageOf(evaluator.Call(*p.Find("Both"), { Int(1) }, SourceLocation())), "a constant here is a number, a bool or a character"));
		TEST_ASSERT(Contains(MessageOf(evaluator.Call(*p.Find("Divide"), {}, SourceLocation())), "'Divide' does not take 0 arguments"));
		TEST_ASSERT(Contains(MessageOf(evaluator.Call(*p.Find("Divide"), { Int(0) }, SourceLocation())), "divided by zero"));
	}

	void CompileTimeTest::DataComesBackAsTheTargetLaysItOut()
	{
		Program p("struct Table { public int32[4] v; }\n"
			"[compile_time] Table Squares() { Table t; for (int32 k = 0; k < 4; ++k) t.v[k] = k * k; return t; }\n");
		CompileTimeEvaluator evaluator(p.Model());
		const QueryResult<std::vector<uint8>> table = evaluator.CallForData(*p.Find("Squares"), {}, SourceLocation());
		TEST_ASSERT(table.Ok() && table.value.size() == 16);
		int32 values[4] = {};
		if (table.value.size() == 16) std::memcpy(values, table.value.data(), 16);
		TEST_ASSERT(values[0] == 0 && values[1] == 1 && values[2] == 4 && values[3] == 9);
	}

	void CompileTimeTest::EveryConstantPositionCallsThroughTheModel()
	{
		Program p("[compile_time] int32 Square(int32 x) { return x * x; }\n"
			"namespace Ops { [compile_time] int32 Twice(int32 x) { return x + x; } }\n"
			"const int32 Nine = Square(3);\n"
			"const int32 Chained = Square(Nine) + Ops::Twice(1);\n"
			"int32[Square(2)] g_Table;\n"
			"struct Holder { public int32[Ops::Twice(3)] values; }\n"
			"enum Level { Low = Square(2), High = Ops::Twice(8) }\n"
			"template<int32 N> struct Fixed { public int32[N] items; }\n"
			"struct UsesFixed { public Fixed<(Square(2))> f; }\n" // a call in a template argument is parenthesized: F(x) there is a signature
			"[Limit(Square(5))] struct Tagged { public int32 v; }\n");
		const ProgramModel& model = p.Model();
		TEST_ASSERT(model.QueryConstant("Nine").value.AsInt() == 9);
		TEST_ASSERT(model.QueryConstant("Chained").value.AsInt() == 83);
		TEST_ASSERT(model.QueryConstant("Level::Low").value.AsInt() == 4);
		TEST_ASSERT(model.QueryConstant("Level::High").value.AsInt() == 16);
		auto table = model.QueryVariable("g_Table");
		TEST_ASSERT(table.Ok() && table.value.type && table.value.type->GetSize() == 16);
		const ObjectType* holder = nullptr;
		const ObjectType* usesFixed = nullptr;
		const ObjectType* tagged = nullptr;
		for (const ObjectType* object : model.ObjectTypes())
		{
			if (object->GetName() == "Holder") holder = object;
			if (object->GetName() == "UsesFixed") usesFixed = object;
			if (object->GetName() == "Tagged") tagged = object;
		}
		TEST_ASSERT(holder && holder->GetSize() == 24); // a field's extent folds while the model resolves
		TEST_ASSERT(usesFixed && usesFixed->GetSize() == 16); // a template value argument
		TEST_ASSERT(tagged != nullptr);
		if (!tagged) return;
		Core::Vector<const AstNode*> declarations;
		declarations.Push(tagged->GetDeclNode());
		auto annotations = ReflectedAnnotations(model, declarations, AnnotationPlace::Type, model.TypeScope(tagged));
		TEST_ASSERT(annotations.Ok() && annotations.value.Count() == 1);
		TEST_ASSERT(annotations.Ok() && annotations.value[0].arguments[0].value.AsInt() == 25); // an annotation argument
	}

	// A field's extent folds in pass 2, before the struct the callee builds only inside its body is laid out (it is
	// declared after the field's type): the body's types are completed as it lowers, not refused as "does not compile".
	void CompileTimeTest::ABodysOwnTypesAreCompletedInPassTwo()
	{
		Program p("[compile_time] int32 Area() { Rect r; r.w = 3; r.h = 4; Rect copy = r; return copy.w * copy.h; }\n"
			"struct Holder { public int32[Area()] cells; }\n"
			"enum Size { Whole = Area() }\n"
			"struct Rect { public int32 w; public int32 h; }\n");
		TEST_ASSERT(!p.Reported("does not compile"));
		const ObjectType* holder = nullptr;
		for (const ObjectType* object : p.Model().ObjectTypes())
			if (object->GetName() == "Holder") holder = object;
		TEST_ASSERT(holder && holder->GetSize() == 48);
		TEST_ASSERT(p.Model().QueryConstant("Size::Whole").value.AsInt() == 12);
	}

	void CompileTimeTest::AConstantCallsOnlyAMarkedFunction()
	{
		Program p("int32 Plain(int32 x) { return x; }\n"
			"[compile_time] int32 Marked(int32 x) { return x; }\n"
			"const int32 Later = Plain(4);\n"
			"int32[Plain(4)] g_Bad;\n"
			"int32[Marked(4)] g_Good;\n");
		// A const whose initializer calls an unmarked function still starts at run time, as before D-91.
		TEST_ASSERT(p.Model().QueryConstant("Later").value.IsNone() || !p.Model().QueryConstant("Later").Ok());
		TEST_ASSERT(p.Reported("'Plain' is not [compile_time]: only a function marked [compile_time] runs in a constant"));
		auto good = p.Model().QueryVariable("g_Good");
		TEST_ASSERT(good.Ok() && good.value.type && good.value.type->GetSize() == 16);
	}

	void CompileTimeTest::AGlobalTableStartsAsData()
	{
		Program p("struct Table { public int32[4] v; }\n"
			"struct Linked { public int32* next; }\n"
			"[compile_time] Table Squares() { Table t; for (int32 k = 0; k < 4; ++k) t.v[k] = k * k; return t; }\n"
			"[compile_time] Linked Empty() { Linked l; return l; }\n"
			"[compile_time] Table Broken() { Table t; t.v[0] = 1 / t.v[1]; return t; }\n"
			"const Table g_Squares = Squares();\n"
			"Linked g_Linked = Empty();\n"
			"Table g_Broken = Broken();\n");
		ErrorTracker errors;
		HIRFunction round;
		StartedGlobals started;
		std::vector<ConstantStart> constants;
		LowerGlobalInit(p.Model(), errors, round, started, constants);
		const ConstantStart* squares = nullptr;
		bool linkedIsData = false;
		bool brokenIsData = false;
		for (const ConstantStart& constant : constants)
		{
			if (constant.variable->plainName == "g_Squares") squares = &constant;
			linkedIsData = linkedIsData || constant.variable->plainName == "g_Linked";
			brokenIsData = brokenIsData || constant.variable->plainName == "g_Broken";
		}
		TEST_ASSERT(squares && squares->data.bytes.size() == 16);
		int32 values[4] = {};
		if (squares && squares->data.bytes.size() == 16) std::memcpy(values, squares->data.bytes.data(), 16);
		TEST_ASSERT(values[0] == 0 && values[1] == 1 && values[2] == 4 && values[3] == 9);
		TEST_ASSERT(!linkedIsData); // a pointer the evaluator made means nothing in the image: it starts as code
		TEST_ASSERT(!brokenIsData); // a call that cannot run starts as code, before main, as it did
	}
} // namespace Testing
