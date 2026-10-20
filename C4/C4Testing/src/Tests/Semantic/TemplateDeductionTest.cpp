#include "TemplateDeductionTest.h"
#include "Model/TypeHandle.h"
#include "Model/Types/FunPtrType.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/PrattParser.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include <memory>

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

	static const ObjectType* Object(const ProgramModel& model, const char* name)
	{
		auto t = model.QueryType(name);
		if (!t.Ok()) return nullptr;
		return dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(t.value));
	}

	static const Type* FieldType(const ObjectType* obj, const char* name)
	{
		for (const auto& f : obj->GetFields())
			if (f.mName == name) return f.m_Type;
		return nullptr;
	}

	struct Call
	{
		const FunctionType* sig = nullptr;
		const FunctionRecord* record = nullptr;
		Core::String error;
	};

	static Call Resolve(const ProgramModel& model, const char* name, Core::Vector<const Type*> args, const Type* receiver = nullptr)
	{
		Call c;
		auto r = model.QueryFunction(name, receiver, nullptr, args, {}, &c.record);
		if (r.Ok()) c.sig = r.value;
		else c.error = r.message;
		return c;
	}

	void TemplateDeductionTest::ArgumentsBindByPosition()
	{
		// BUG-066: a value parameter before a type parameter binds the value, not the first type.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<int32 N, T> struct Arr { public T[N] items; }\n"
			"template<T, int32 N, U> struct Triple { public T first; public U last; }\n"
			"alias A = Arr<3, int32>;\n"
			"alias T3 = Triple<int32, 7, float>;\n");
		const ObjectType* a = Object(model, "A");
		TEST_ASSERT(a != nullptr);
		TEST_ASSERT_EQ((int)a->GetSize(), 12);
		const ObjectType* t = Object(model, "T3");
		TEST_ASSERT(t != nullptr && t->GetTemplateArguments().Count() == 3);
		TypeStore* ts = model.TypeStorePtr();
		TEST_ASSERT(t->GetTemplateArguments()[0].type == ts->Builtin(TypeKind::Int32));
		TEST_ASSERT(t->GetTemplateArguments()[1].type == nullptr && t->GetTemplateArguments()[1].value.AsInt() == 7);
		TEST_ASSERT(t->GetTemplateArguments()[2].type == ts->Builtin(TypeKind::Float32));
		TEST_ASSERT(FieldType(t, "last") == ts->Builtin(TypeKind::Float32));
	}

	void TemplateDeductionTest::DefaultsMayNameEarlierParameters()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<T, U = T*> struct Holder { public T value; public U link; }\n"
			"template<T = int32, int32 N = 4> struct Buf { public T[N] data; }\n"
			"alias H = Holder<int64>;\n"
			"alias B = Buf<>;\n"
			"alias F = Buf<float>;\n");
		TypeStore* ts = model.TypeStorePtr();
		const ObjectType* h = Object(model, "H");
		TEST_ASSERT(h != nullptr);
		TEST_ASSERT(FieldType(h, "link") == ts->Pointer(ts->Builtin(TypeKind::Int64)));
		const ObjectType* b = Object(model, "B");
		TEST_ASSERT(b != nullptr && b->GetTemplateArguments().Count() == 2);
		TEST_ASSERT(b->GetTemplateArguments()[0].type == ts->Builtin(TypeKind::Int32));
		TEST_ASSERT_EQ((int)b->GetTemplateArguments()[1].value.AsInt(), 4);
		TEST_ASSERT_EQ((int)Object(model, "F")->GetSize(), 16);
	}

	void TemplateDeductionTest::DeducesThroughParameterShapes()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<T, int32 N> int32 Len(T[N]& items) { return N; }\n"
			"template<T> T Read(T* p) { return *p; }\n"
			"template<T> T Decayed(T p) { return p; }\n"
			"template<R, A> R Apply(funcptr<R(A)> f, A a) { return f(a); }\n");
		TypeStore* ts = model.TypeStorePtr();
		const Type* i32 = ts->Builtin(TypeKind::Int32);
		const Type* arr7 = ts->Array(i32, 7);
		Call len = Resolve(model, "Len", { arr7 });
		TEST_ASSERT(len.sig != nullptr);
		TEST_ASSERT(TypeHandle::UnwrapToValue(len.sig->GetParameters()[0].m_Type) == arr7); // T = int32, N = 7
		Call read = Resolve(model, "Read", { ts->Pointer(ts->Builtin(TypeKind::Float32)) });
		TEST_ASSERT(read.sig != nullptr && read.sig->GetReturnType() == ts->Builtin(TypeKind::Float32));
		Call decayed = Resolve(model, "Decayed", { ts->Array(i32, 3) });
		TEST_ASSERT(decayed.sig != nullptr && decayed.sig->GetReturnType() == ts->Pointer(i32)); // by-value T: array decays
		Core::Vector<const Type*> one; one.Push(i32);
		Call apply = Resolve(model, "Apply", { ts->FunPtr(ts->Builtin(TypeKind::Int64), one), i32 });
		TEST_ASSERT(apply.sig != nullptr && apply.sig->GetReturnType() == ts->Builtin(TypeKind::Int64));
	}

	void TemplateDeductionTest::DeducesTemplateInstancesAndTheirBases()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<T> struct Box { public T value; }\n"
			"template<T> T Unbox(const Box<T>& b) { return b.value; }\n"
			"struct Derived : Box<int64> { public int32 extra; }\n"
			"alias FBox = Box<float>;\n");
		TypeStore* ts = model.TypeStorePtr();
		Call direct = Resolve(model, "Unbox", { Object(model, "FBox") });
		TEST_ASSERT(direct.sig != nullptr && direct.sig->GetReturnType() == ts->Builtin(TypeKind::Float32));
		Call viaBase = Resolve(model, "Unbox", { Object(model, "Derived") });
		TEST_ASSERT(viaBase.sig != nullptr && viaBase.sig->GetReturnType() == ts->Builtin(TypeKind::Int64));
	}

	void TemplateDeductionTest::ConflictingDeductionsNameBothTypes()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<T> T Larger(T a, T b) { return a > b ? a : b; }\n"
			"template<T> T Make() { return (T)0; }\n");
		TypeStore* ts = model.TypeStorePtr();
		Call bad = Resolve(model, "Larger", { ts->Builtin(TypeKind::Int32), ts->Builtin(TypeKind::Float32) });
		TEST_ASSERT(bad.sig == nullptr);
		TEST_ASSERT(bad.error.find("conflicting deductions for template parameter 'T': 'int32' and 'float'") != std::string::npos);
		Call missing = Resolve(model, "Make", {});
		TEST_ASSERT(missing.sig == nullptr);
		TEST_ASSERT(missing.error.find("no argument for template parameter 'T' of 'Make'") != std::string::npos);
	}

	void TemplateDeductionTest::WrittenArgumentsAreNotDeduced()
	{
		// `Id<int64>(5)`: the written int64 is final; the int32 argument converts instead of conflicting.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "template<T> T Id(T v) { return v; }\n");
		Ref<Scripting::Source> text = Scripting::Source::FromMemory("Id<int64>(5)");
		Scripting::Lexer lexer(*text, 0);
		ErrorTracker errors;
		Scripting::ParserState state(lexer, errors);
		Ref<AstNode> parsed = PrattParser::ParseExpression(state);
		const auto* call = dynamic_cast<const FunctionCallNode*>(parsed.Get());
		TEST_ASSERT(call != nullptr && call->GetTemplateArgs() != nullptr);
		TypeStore* ts = model.TypeStorePtr();
		Core::Vector<const Type*> args; args.Push(ts->Builtin(TypeKind::Int32));
		auto written = model.QueryFunction("Id", nullptr, call->GetTemplateArgs().Get(), args);
		TEST_ASSERT(written.Ok());
		TEST_ASSERT(written.value->GetReturnType() == ts->Builtin(TypeKind::Int64));
		Call deduced = Resolve(model, "Id", { ts->Builtin(TypeKind::Int32) });
		TEST_ASSERT(deduced.sig != nullptr && deduced.sig->GetReturnType() == ts->Builtin(TypeKind::Int32));
	}

	void TemplateDeductionTest::FunctionsBeatEqualTemplateInstances()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"int32 Pick(int32 x) { return 1; }\n"
			"template<T> int32 Pick(T x) { return 2; }\n");
		TypeStore* ts = model.TypeStorePtr();
		Call exact = Resolve(model, "Pick", { ts->Builtin(TypeKind::Int32) });
		TEST_ASSERT(exact.record != nullptr && exact.record->templateOrigin == TemplateOrigin::None);
		Call other = Resolve(model, "Pick", { ts->Builtin(TypeKind::Float32) });
		TEST_ASSERT(other.record != nullptr && other.record->templateOrigin == TemplateOrigin::FunctionTemplate);
	}

	void TemplateDeductionTest::ExtensionTemplatesAttachToInstances()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<T> struct Pair { public T first; public T second; }\n"
			"template<T> T Sum() extends Pair<T> { return first + second; }\n"
			"alias IntPair = Pair<int32>;\n");
		const ObjectType* p = Object(model, "IntPair");
		TEST_ASSERT(p != nullptr);
		bool attached = false;
		for (const auto& m : p->GetMethods()) attached = attached || (m.mName == "Sum" && m.m_IsExtensionMethod);
		TEST_ASSERT(attached);
		Call sum = Resolve(model, "Sum", {}, p);
		TEST_ASSERT(sum.record != nullptr && sum.record->isExtensionMethod && sum.record->containingType == p);
		TEST_ASSERT(sum.sig->GetReturnType() == model.TypeStorePtr()->Builtin(TypeKind::Int32));
	}
} // namespace Testing
