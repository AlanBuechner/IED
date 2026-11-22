#include "MirInterpreterTest.h"
#include "Backend/HIR/LoweringDriver.h"
#include "Backend/MIR/MirInterpreter.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/ErrorTracker.h"
#include "Model/ProgramModel.h"
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		// A compiled program whose functions the interpreter runs, each lowered once.
		class Program
		{
		public:
			explicit Program(const std::string& source, CtfeLimits limits = {})
				: m_Interpreter([this](const FunctionRecord& record) { return Body(record); }, limits)
			{
				m_Compiler.RegisterInMemory("t.cf", source.c_str());
				m_Compiler.CompileAll();
				m_Compiler.AnalyzeProgram();
				m_Model = &m_Compiler.GetProgramModelForLowering();
			}

			// `name` run on integer or float arguments, each made the parameter's type.
			QueryResult<CtfeBytes> Run(const char* name, const std::vector<ConstantValue>& arguments)
			{
				const FunctionRecord* record = Find(name);
				if (!record) return QueryResult<CtfeBytes>::Fail(QueryError::NotFound, std::string("no function ") + name);
				std::vector<CtfeBytes> bytes(arguments.size());
				for (size_t i = 0; i < arguments.size(); ++i) MirInterpreter::Encode(arguments[i], record->type->GetParameters()[i].m_Type, bytes[i]);
				return m_Interpreter.Call(*record, bytes, SourceLocation());
			}

			// The result as the function's return type says.
			ConstantValue Value(const char* name, const std::vector<ConstantValue>& arguments)
			{
				const QueryResult<CtfeBytes> result = Run(name, arguments);
				if (!result.Ok()) return ConstantValue();
				return MirInterpreter::Decode(result.value, Find(name)->type->GetReturnType());
			}

			std::string Error(const char* name, const std::vector<ConstantValue>& arguments)
			{
				const QueryResult<CtfeBytes> result = Run(name, arguments);
				return result.Ok() ? std::string() : std::string(result.message.c_str());
			}

		private:
			const FunctionRecord* Find(const char* name) const
			{
				Core::Vector<const FunctionRecord*> records;
				m_Model->AllFunctionRecords(records);
				for (const FunctionRecord* record : records)
					if (record->name == name && record->bodyNode) return record;
				return nullptr;
			}

			QueryResult<const HIRFunction*> Body(const FunctionRecord& record)
			{
				auto made = m_Bodies.find(&record);
				if (made == m_Bodies.end())
					made = m_Bodies.emplace(&record, std::make_unique<HIRFunction>(LowerFunction(*m_Model, m_Errors, &record))).first;
				if (!made->second->complete) return QueryResult<const HIRFunction*>::Fail(QueryError::NotAValue, "does not compile");
				return QueryResult<const HIRFunction*>(made->second.get());
			}

			CompilerInstance m_Compiler;
			ProgramModel* m_Model = nullptr;
			ErrorTracker m_Errors;
			std::unordered_map<const FunctionRecord*, std::unique_ptr<HIRFunction>> m_Bodies;
			MirInterpreter m_Interpreter;
		};

		ConstantValue Int(int64 value) { return ConstantValue::Int(value); }
		ConstantValue Real(double value) { return ConstantValue::Float(value); }
		bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }
	}

	void MirInterpreterTest::ScalarsFold()
	{
		Program p("int32 Square(int32 x) { return x * x; }\n"
			"double Half(double v) { return v / 2.0; }\n"
			"bool IsEven(int32 n) { return n % 2 == 0; }\n"
			"float Mix(float a, int32 b) { return a * b + 0.5f; }\n");
		TEST_ASSERT(p.Value("Square", { Int(7) }).AsInt() == 49);
		TEST_ASSERT(p.Value("Half", { Real(5.0) }).floatValue == 2.5);
		TEST_ASSERT(p.Value("IsEven", { Int(10) }).booleanValue);
		TEST_ASSERT(!p.Value("IsEven", { Int(7) }).booleanValue);
		TEST_ASSERT(p.Value("Mix", { Real(1.5), Int(3) }).floatValue == 5.0);
	}

	void MirInterpreterTest::ArithmeticWrapsAsAtRunTime()
	{
		Program p("int32 Next(int32 x) { return x + 1; }\n"
			"uint8 NextByte(uint8 b) { return b + 1; }\n"
			"int32 Halve(int32 x) { return x >> 1; }\n"
			"uint32 Top(uint32 x) { return x / 3; }\n"
			"int32 Rem(int32 a, int32 b) { return a % b; }\n");
		TEST_ASSERT(p.Value("Next", { Int(2147483647) }).AsInt() == -2147483648ll);
		TEST_ASSERT(p.Value("NextByte", { Int(255) }).AsInt() == 0);
		TEST_ASSERT(p.Value("Halve", { Int(-8) }).AsInt() == -4);
		TEST_ASSERT(p.Value("Top", { Int(4294967295ll) }).AsInt() == 1431655765);
		TEST_ASSERT(p.Value("Rem", { Int(-7), Int(3) }).AsInt() == -1);
	}

	void MirInterpreterTest::ValueTypesAndMethods()
	{
		Program p("struct V2 { public float x; public float y; public float Dot(const V2& o) const { return x * o.x + y * o.y; } }\n"
			"float Len2(float a, float b) { V2 v; v.x = a; v.y = b; return v.Dot(v); }\n"
			"V2 Both(float a) { return V2{ a, a + 1.0f }; }\n"
			"float Sum(float a) { V2 v = Both(a); return v.x + v.y; }\n");
		TEST_ASSERT(p.Value("Len2", { Real(3.0), Real(4.0) }).floatValue == 25.0);
		TEST_ASSERT(p.Value("Sum", { Real(2.0) }).floatValue == 5.0);
		const QueryResult<CtfeBytes> pair = p.Run("Both", { Real(1.0) });
		TEST_ASSERT(pair.Ok() && pair.value.size() == 8);
		float second = 0;
		if (pair.value.size() == 8) std::memcpy(&second, pair.value.data() + 4, 4);
		TEST_ASSERT(second == 2.0f);
	}

	void MirInterpreterTest::LoopsBranchesAndSwitches()
	{
		Program p("int32 SumTo(int32 n) { int32 s = 0; for (int32 i = 1; i <= n; ++i) s += i; return s; }\n"
			"int32 Classify(int32 k) { switch (k) { case 1: return 10; case 2: return 20; default: return 0; } }\n"
			"int32 FirstSquareOver(int32 limit) { int32 i = 0; while (true) { if (i * i > limit) break; ++i; } return i; }\n"
			"int32 Collatz(int32 n) { int32 steps = 0; do { n = n % 2 == 0 ? n / 2 : 3 * n + 1; ++steps; } while (n != 1); return steps; }\n");
		TEST_ASSERT(p.Value("SumTo", { Int(100) }).AsInt() == 5050);
		TEST_ASSERT(p.Value("Classify", { Int(2) }).AsInt() == 20);
		TEST_ASSERT(p.Value("Classify", { Int(9) }).AsInt() == 0);
		TEST_ASSERT(p.Value("FirstSquareOver", { Int(50) }).AsInt() == 8);
		TEST_ASSERT(p.Value("Collatz", { Int(27) }).AsInt() == 111);
	}

	void MirInterpreterTest::RecursionRuns()
	{
		Program p("int64 Fact(int32 n) { return n <= 1 ? 1 : n * Fact(n - 1); }\n"
			"int32 Fib(int32 n) { if (n < 2) return n; return Fib(n - 1) + Fib(n - 2); }\n");
		TEST_ASSERT(p.Value("Fact", { Int(15) }).AsInt() == 1307674368000ll);
		TEST_ASSERT(p.Value("Fib", { Int(20) }).AsInt() == 6765);
	}

	void MirInterpreterTest::ArraysBuildTables()
	{
		Program p("int32 SquareAt(int32 i) { int32[8] t; for (int32 k = 0; k < 8; ++k) t[k] = k * k; return t[i]; }\n"
			"struct Table { public int32[4] v; }\n"
			"Table Build() { Table t; for (int32 k = 0; k < 4; ++k) t.v[k] = k + 10; return t; }\n");
		TEST_ASSERT(p.Value("SquareAt", { Int(5) }).AsInt() == 25);
		TEST_ASSERT(Contains(p.Error("SquareAt", { Int(9) }), "past the end"));
		const QueryResult<CtfeBytes> table = p.Run("Build", {});
		TEST_ASSERT(table.Ok() && table.value.size() == 16);
		int32 last = 0;
		if (table.value.size() == 16) std::memcpy(&last, table.value.data() + 12, 4);
		TEST_ASSERT(last == 13);
	}

	void MirInterpreterTest::ALoopThatDoesNotEndHitsTheStepLimit()
	{
		CtfeLimits limits;
		limits.steps = 10000;
		Program p("int32 Forever() { int32 i = 0; while (true) { i = i + 1; } }\n", limits);
		const std::string error = p.Error("Forever", {});
		TEST_ASSERT(Contains(error, "more than 10000 steps"));
	}

	void MirInterpreterTest::RecursionThatDoesNotEndHitsTheDepthLimit()
	{
		Program p("int32 Down(int32 n) { return Down(n + 1); }\n");
		TEST_ASSERT(Contains(p.Error("Down", { Int(0) }), "more than 256 calls deep"));
	}

	void MirInterpreterTest::WhatCompileTimeCannotDoIsAnError()
	{
		Program p("int32 g = 3;\n"
			"int32 Alloc() { int32* q = new int32(); return *q; }\n"
			"int32 Divide(int32 a) { return 10 / a; }\n"
			"int32 ReadGlobal() { return g; }\n"
			"int32 Thrower(int32 a) { if (a > 0) throw a; return a; }\n"
			"int32 Shift(int32 a) { return 1 << a; }\n");
		TEST_ASSERT(Contains(p.Error("Alloc", {}), "cannot allocate on the heap"));
		TEST_ASSERT(Contains(p.Error("Divide", { Int(0) }), "divided by zero"));
		TEST_ASSERT(p.Value("Divide", { Int(5) }).AsInt() == 2);
		TEST_ASSERT(Contains(p.Error("ReadGlobal", {}), "global variable"));
		TEST_ASSERT(Contains(p.Error("Thrower", { Int(1) }), "threw an exception"));
		TEST_ASSERT(p.Value("Thrower", { Int(-1) }).AsInt() == -1);
		TEST_ASSERT(Contains(p.Error("Shift", { Int(40) }), "shifted a 32-bit value by 40 bits"));
	}
} // namespace Testing
