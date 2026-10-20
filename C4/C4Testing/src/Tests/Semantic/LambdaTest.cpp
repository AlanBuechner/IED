#include "LambdaTest.h"
#include "Backend/HIR/HIR.h"
#include "Backend/HIR/LambdaCaptures.h"
#include "Compiler/ErrorTracker.h"
#include "Model/TypeHandle.h"
#include "Model/Types/FuncType.h"
#include "Parser/PrattParser.h"
#include "Parser/Nodes/Expressions/LambdaNode.h"
#include "SourceManager/Source.h"
#include <algorithm>
#include <memory>

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

		// The parser state a lambda expression is read with; members are declared in initialization order.
		struct ParsedLambda
		{
			Ref<Scripting::Source> text;
			Scripting::Lexer lexer;
			Scripting::ErrorTracker errors;
			std::unique_ptr<Scripting::ParserState> state;
			Ref<AstNode> node;

			explicit ParsedLambda(const char* source)
				: text(Scripting::Source::FromMemory(std::string(source))), lexer(*text, 0)
			{
				state = std::make_unique<Scripting::ParserState>(lexer, errors);
				node = PrattParser::ParseExpression(*state);
			}

			const LambdaNode* Lambda() const { return dynamic_cast<const LambdaNode*>(node.Get()); }
		};

		bool Has(const Core::Vector<Core::String>& names, const char* name)
		{
			return std::find(names.begin(), names.end(), Core::String(name)) != names.end();
		}

		const FunctionRecord* Record(const ProgramModel& model, const char* name)
		{
			Core::Vector<const FunctionRecord*> records;
			model.AllFunctionRecords(records);
			for (const FunctionRecord* r : records)
				if (r->name == name || r->mangledName.rfind(Core::String(name) + "_", 0) == 0) return r;
			return nullptr;
		}

		const Closure* OnlyClosure(const ProgramModel& model)
		{
			for (const ObjectType* t : model.ObjectTypes())
				if (const Closure* c = model.ClosureOf(t)) return c;
			return nullptr;
		}
	}

	void LambdaTest::CaptureListsParse()
	{
		ParsedLambda value("[=, &y, n = 1 + 2, this](int32 a) mutable -> int32 { return a; }");
		const LambdaNode* l = value.Lambda();
		TEST_ASSERT(l != nullptr && value.errors.GetErrors().Count() == 0);
		TEST_ASSERT(l->GetCaptureDefault() == CaptureDefault::ByValue && l->IsMutable());
		TEST_ASSERT_EQ((int)l->GetCaptures().Count(), 3);
		TEST_ASSERT(l->GetCaptures()[0].byReference && l->GetCaptures()[0].name.GetText() == "y");
		TEST_ASSERT(l->GetCaptures()[1].kind == LambdaCapture::Kind::Init && l->GetCaptures()[1].init);
		TEST_ASSERT(l->GetCaptures()[2].kind == LambdaCapture::Kind::This);

		ParsedLambda copy("[*this, &]() {}");
		TEST_ASSERT(copy.errors.GetErrors().Count() != 0); // a default must come first

		ParsedLambda byRef("[&, x]() { }");
		TEST_ASSERT(byRef.Lambda() && byRef.Lambda()->GetCaptureDefault() == CaptureDefault::ByReference);
		TEST_ASSERT(byRef.Lambda()->GetCaptures()[0].name.GetText() == "x" && !byRef.Lambda()->GetCaptures()[0].byReference);
	}

	void LambdaTest::OuterUsesSkipParametersLocalsAndMembers()
	{
		ParsedLambda parsed(
			"[=](int32 a) -> int32 {\n"
			"  int32 local = a + outer;\n"
			"  for (int32 i = 0; i < 3; i++) local += i + count;\n"
			"  for (int32 v : items) local += v;\n"
			"  try { local++; } catch (int32 e) { local += e; }\n"
			"  local += obj.field + ptr->other + Ns::value + this->member;\n"
			"  return local;\n"
			"}");
		TEST_ASSERT(parsed.Lambda() != nullptr);
		const OuterUses uses = FindOuterUses(*parsed.Lambda());
		for (const char* outer : { "outer", "count", "items", "obj", "ptr" }) TEST_ASSERT(Has(uses.names, outer));
		for (const char* inner : { "a", "local", "i", "v", "e", "field", "other", "value", "member" }) TEST_ASSERT(!Has(uses.names, inner));
		TEST_ASSERT(uses.usesThis);
	}

	void LambdaTest::NestedLambdaUsesCountInTheEnclosingScope()
	{
		ParsedLambda parsed("[&](int32 p) { int32 mine = 1; [=]() { use(mine + p + far); }(); [x]() {}(); }");
		const OuterUses uses = FindOuterUses(*parsed.Lambda());
		TEST_ASSERT(Has(uses.names, "far") && Has(uses.names, "x") && Has(uses.names, "use"));
		TEST_ASSERT(!Has(uses.names, "mine") && !Has(uses.names, "p"));
		TEST_ASSERT(!uses.usesThis);
	}

	void LambdaTest::ClosureTypesHoldTheirCaptures()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Big { int64[4] words; }\n"
			"int32 F() { int32 x = 1; Big b; int32 y = 2;\n"
			"  func<int32()> f = [x, &b, n = y + 1]() -> int32 { return x + n + (int32)b.words[0]; };\n"
			"  return f(); }\n");
		ErrorTracker errors;
		HIRFunction fn = LowerFunction(model, errors, Record(model, "F"));
		TEST_ASSERT(fn.complete);
		const Closure* c = OnlyClosure(model);
		TEST_ASSERT(c != nullptr && c->captures.Count() == 3 && !c->call->isStatic);
		TypeStore* ts = model.TypeStorePtr();
		TEST_ASSERT(c->captures[0].kind == ClosureCapture::Kind::Value && c->captures[0].type == ts->Builtin(TypeKind::Int32));
		TEST_ASSERT(c->captures[1].kind == ClosureCapture::Kind::Reference && c->captures[1].name == "b");
		TEST_ASSERT(c->captures[2].kind == ClosureCapture::Kind::Value && c->captures[2].name == "n");
		TEST_ASSERT(c->type->GetFields()[1].m_Type->GetKind() == TypeKind::Pointer);
		TEST_ASSERT_EQ((int)c->type->GetSize(), 24); // int32, pointer, int32 at offsets 0, 8, 16
	}

	void LambdaTest::FuncTypesAreSixteenBytesAndInterned()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "alias F = func<int32(float, int64)>;\nalias G = func<int32(float, int64)>;\n");
		const Type* f = TypeHandle::UnwrapToValue(model.QueryType("F").value);
		const Type* g = TypeHandle::UnwrapToValue(model.QueryType("G").value);
		TEST_ASSERT(f != nullptr && f == g);
		TEST_ASSERT(f->GetKind() == TypeKind::Func && f->GetSize() == FuncType::kSize && f->GetSize() == 16);
		TEST_ASSERT(TypeHandle::MayHoldPointers(f) && TypeHandle::HasTruthValue(f));
	}

	void LambdaTest::CallablesConvertToFuncAndFuncptr()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 Takes(func<int32(int32)> f) { return 1; }\n"
			"int32 TakesPointer(funcptr<int32(int32)> f) { return 2; }\n"
			"int32 Double(int32 x) { return x * 2; }\n"
			"int32 G() { int32 k = 3;\n"
			"  return Takes([k](int32 v) -> int32 { return v + k; }) + Takes(Double) + TakesPointer([](int32 v) -> int32 { return v; })\n"
			"    + Takes(null); }\n");
		ErrorTracker errors;
		HIRFunction fn = LowerFunction(model, errors, Record(model, "G"));
		TEST_ASSERT(fn.complete);
		TEST_ASSERT_EQ((int)errors.GetErrors().Count(), 0);
		int makes = 0;
		for (const auto& node : fn.m_Nodes) makes += node->kind == HIRNode::Kind::MakeFunc ? 1 : 0;
		TEST_ASSERT_EQ(makes, 3); // the closure, the funcptr of Double, null
	}

	void LambdaTest::FunctionPointersBindOnlyVoidPointers()
	{
		// BUG-102: a funcptr is not a PointerType; it binds `void*` (a universal handle) and no other pointer.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 Opaque(void* p) { return 1; }\nint32 Typed(int32* p) { return 2; }\n");
		TypeStore* ts = model.TypeStorePtr();
		Core::Vector<const Type*> none;
		Core::Vector<const Type*> args;
		args.Push(ts->FunPtr(ts->Builtin(TypeKind::Int32), none));
		TEST_ASSERT(model.QueryFunction("Opaque", nullptr, nullptr, args).Ok());
		TEST_ASSERT(!model.QueryFunction("Typed", nullptr, nullptr, args).Ok());
	}

	void LambdaTest::NestedBodiesDoNotSeeEnclosingLocals()
	{
		// BUG-101: a local function's body is its own frame; the enclosing local is not a variable there.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "int32 H() { int32 b = 7; int32 Inner() { return b; } return Inner(); }\n");
		ErrorTracker errors;
		LowerFunction(model, errors, Record(model, "H"));
		bool named = false;
		for (const auto& e : errors.GetErrors()) named = named || e.message.find("no variable named b") != std::string::npos;
		TEST_ASSERT(named);
	}
} // namespace Testing
