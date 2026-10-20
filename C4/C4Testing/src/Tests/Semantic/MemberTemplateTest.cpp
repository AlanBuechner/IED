#include "MemberTemplateTest.h"
#include "Model/TypeHandle.h"
#include "Parser/AstNode.h"
#include "Parser/PrattParser.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
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

		const ObjectType* Object(const ProgramModel& model, const char* name)
		{
			auto t = model.QueryType(name);
			return t.Ok() ? dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(t.value)) : nullptr;
		}

		const FunctionRecord* Resolve(const ProgramModel& model, const char* name, Core::Vector<const Type*> args, const Type* receiver = nullptr,
			const TemplateArgumentListNode* written = nullptr)
		{
			const FunctionRecord* rec = nullptr;
			auto r = model.QueryFunction(name, receiver, written, args, {}, &rec);
			return r.Ok() ? rec : nullptr;
		}

		const SymbolInfo* StaticMember(const ProgramModel& model, const ObjectType* owner, const char* name)
		{
			const Scope* scope = owner ? model.TypeScope(owner) : nullptr;
			return scope ? scope->Find(name) : nullptr;
		}
	}

	void MemberTemplateTest::MemberTemplateIsAMethodOfItsOwner()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct P { public int32 k; template<U> public static int32 Kind(U u) { return 1; } template<U> public U Scale(U u) { return u; } }\n");
		const ObjectType* p = Object(model, "P");
		TypeStore* ts = model.TypeStorePtr();
		const FunctionRecord* kind = Resolve(model, "Kind", { ts->Builtin(TypeKind::Float32) }, p);
		TEST_ASSERT(kind && kind->containingType == p && kind->isStatic && !kind->isExtensionMethod);
		const FunctionRecord* scale = Resolve(model, "Scale", { ts->Builtin(TypeKind::Int64) }, p);
		TEST_ASSERT(scale && scale->containingType == p && !scale->isStatic);
		TEST_ASSERT(scale->type->GetReturnType() == ts->Builtin(TypeKind::Int64));
	}

	void MemberTemplateTest::QualifiedCallFindsMemberTemplates()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler, "struct P { template<U> public static int32 Kind(U u) { return 1; } }\n");
		const FunctionRecord* rec = Resolve(model, "P::Kind", { model.TypeStorePtr()->Builtin(TypeKind::Int8) });
		TEST_ASSERT(rec && rec->containingType == Object(model, "P"));
	}

	void MemberTemplateTest::ClassTemplateMemberTemplateSeesClassParameters()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<T> struct S { public T base; template<U> public T Plus(U u) { return base + (T)u; } }\n"
			"alias S64 = S<int64>;\n");
		const ObjectType* s = Object(model, "S64");
		const FunctionRecord* rec = Resolve(model, "Plus", { model.TypeStorePtr()->Builtin(TypeKind::Float32) }, s);
		TEST_ASSERT(rec && rec->containingType == s);
		TEST_ASSERT(rec->type->GetReturnType() == model.TypeStorePtr()->Builtin(TypeKind::Int64));
	}

	void MemberTemplateTest::OutOfLineDefinitionsAttach()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct P { public int32 k; template<U> public U Out(U u); }\n"
			"template<V> V P::Out(V v) { return v; }\n"
			"template<T> struct S { template<U> public U Later(U u); }\n"
			"template<X> template<U> U S<X>::Later(U u) { return u; }\n"
			"alias S8 = S<int8>;\n");
		const Type* i32 = model.TypeStorePtr()->Builtin(TypeKind::Int32);
		const FunctionRecord* out = Resolve(model, "Out", { i32 }, Object(model, "P"));
		TEST_ASSERT(out && out->bodyNode != nullptr);
		const FunctionRecord* later = Resolve(model, "Later", { i32 }, Object(model, "S8"));
		TEST_ASSERT(later && later->bodyNode != nullptr && later->containingType == Object(model, "S8"));
	}

	void MemberTemplateTest::WrittenArgumentsNameOnlyTemplates()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct P { public int32 Get(int32 v) { return v; } template<U> public U Get(U u) { return u; } }\n");
		auto stream = Source::FromMemory("p.Get<int64>(1)");
		Lexer lexer(*stream, 0);
		ErrorTracker errors;
		ParserState state(lexer, errors);
		auto parsed = PrattParser::ParseExpression(state);
		const auto* call = dynamic_cast<const FunctionCallNode*>(parsed.Get());
		TEST_ASSERT(call && call->GetTemplateArgs());
		const Type* i32 = model.TypeStorePtr()->Builtin(TypeKind::Int32);
		const FunctionRecord* rec = Resolve(model, "Get", { i32 }, Object(model, "P"), call->GetTemplateArgs().Get());
		TEST_ASSERT(rec && rec->type->GetReturnType() == model.TypeStorePtr()->Builtin(TypeKind::Int64));
		const FunctionRecord* plain = Resolve(model, "Get", { i32 }, Object(model, "P"));
		TEST_ASSERT(plain && plain->templateOrigin == TemplateOrigin::None);
	}

	void MemberTemplateTest::InstanceMembersHaveTheirOwnSymbols()
	{
		// BUG-116: both instances' Size() were named `B_Size_0`.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<T> struct B { public int64 Size() { return 0; } }\n"
			"alias B4 = B<int32>;\n"
			"alias B8 = B<int64>;\n");
		const FunctionRecord* four = Resolve(model, "Size", {}, Object(model, "B4"));
		const FunctionRecord* eight = Resolve(model, "Size", {}, Object(model, "B8"));
		TEST_ASSERT(four && eight && four != eight);
		TEST_ASSERT(four->mangledName != eight->mangledName);
	}

	void MemberTemplateTest::InstanceStaticMembersAreTheirOwnObjects()
	{
		// BUG-115: an instance's static data members had no storage.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"template<T> struct C { static int32 hits; static const int64 Size = sizeof(T); }\n"
			"alias C4 = C<int32>;\n"
			"alias C8 = C<int64>;\n");
		const SymbolInfo* hits4 = StaticMember(model, Object(model, "C4"), "hits");
		const SymbolInfo* hits8 = StaticMember(model, Object(model, "C8"), "hits");
		TEST_ASSERT(hits4 && hits8 && hits4 != hits8 && hits4->mangledName != hits8->mangledName);
		TEST_ASSERT(hits4->isStatic && hits4->type == model.TypeStorePtr()->Builtin(TypeKind::Int32));
		const SymbolInfo* size4 = StaticMember(model, Object(model, "C4"), "Size");
		const SymbolInfo* size8 = StaticMember(model, Object(model, "C8"), "Size");
		TEST_ASSERT(size4 && size8 && size4->constValue.AsInt() == 4 && size8->constValue.AsInt() == 8);
	}
} // namespace Testing
