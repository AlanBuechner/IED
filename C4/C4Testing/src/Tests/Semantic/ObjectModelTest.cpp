#include "ObjectModelTest.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/VirtualTables.h"
#include "Backend/CodeGen/Abi.h"
#include "Backend/CodeGen/MsvcNames.h"
#include "Backend/CodeGen/CodeGenDriver.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

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

	static int BaseIndex(const ObjectType* type, const ObjectType* base)
	{
		for (uint32 i = 0; i < type->GetBaseTypes().Count(); ++i)
			if (type->GetBaseTypes()[i] == base) return static_cast<int>(i);
		return -1;
	}

	static const ObjectType::MethodInfo* Method(const ObjectType* type, const char* name)
	{
		for (const auto& m : type->GetMethods())
			if (m.mName == name) return &m;
		return nullptr;
	}

	// --- layout ---------------------------------------------------------------------

	void ObjectModelTest::PolymorphicBasesComeFirst()
	{
		// MSVC: a polymorphic base (the interface) goes first and its vptr is the type's vptr; the class
		// base without virtuals follows it; then the fields.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"class P { public int32 a; }\n"
			"interface I { int32 F(); }\n"
			"class C : P, I { public int32 c; public int32 F() { return 1; } }\n");
		const ObjectType* c = Object(model, "C");
		const ObjectType* p = Object(model, "P");
		const ObjectType* i = Object(model, "I");
		TEST_ASSERT(c && p && i);
		TEST_ASSERT(i->IsInterface());
		TEST_ASSERT(c->GetPrimaryBase() == i);
		TEST_ASSERT(!c->HasOwnVptr());
		TEST_ASSERT_EQ((int)c->GetBaseOffset(BaseIndex(c, i)), 0);
		TEST_ASSERT_EQ((int)c->GetBaseOffset(BaseIndex(c, p)), 8);
		TEST_ASSERT_EQ((int)c->GetFieldOffset("a"), 8);
		TEST_ASSERT_EQ((int)c->GetFieldOffset("c"), 12);
		TEST_ASSERT_EQ((int)c->GetSize(), 16);
	}

	void ObjectModelTest::OwnVptrWithoutPolymorphicBase()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"class P { public int32 a; }\n"
			"class D : P { public int32 d; public virtual int32 F() { return 1; } }\n");
		const ObjectType* d = Object(model, "D");
		TEST_ASSERT(d && d->IsPolymorphic());
		TEST_ASSERT(d->HasOwnVptr());
		TEST_ASSERT(d->GetPrimaryBase() == nullptr);
		TEST_ASSERT_EQ((int)d->GetBaseOffset(0), 8);
		TEST_ASSERT_EQ((int)d->GetFieldOffset("d"), 12);
		TEST_ASSERT_EQ((int)d->GetSize(), 16);
	}

	// --- virtual slots --------------------------------------------------------------

	void ObjectModelTest::SlotsKeepBaseOrderAndSplitOverloads()
	{
		// BUG-054 / BUG-055: inherited slots keep their indices, overrides replace in place, new virtuals
		// are appended, and overloads are separate slots.
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Base { public virtual int32 Bar() { return 1; } public virtual int32 Draw(int32 x) { return 2; } public virtual int32 Draw(float x) { return 3; } }\n"
			"struct Derived : Base { public virtual int32 Foo() { return 4; } public override int32 Bar() { return 5; } public override int32 Draw(float x) { return 6; } }\n");
		const ObjectType* base = Object(model, "Base");
		const ObjectType* derived = Object(model, "Derived");
		TEST_ASSERT(base && derived);
		auto slots = VirtualTables::PrimarySlots(derived);
		TEST_ASSERT_EQ((int)slots.size(), 4);
		TEST_ASSERT(slots[0].introducer == base && slots[0].method->mName == "Bar");
		TEST_ASSERT(slots[1].introducer == base && slots[1].method->mName == "Draw");
		TEST_ASSERT(slots[2].introducer == base && slots[2].method->mName == "Draw");
		TEST_ASSERT(!VirtualTables::SameSignature(*slots[1].method, *slots[2].method));
		TEST_ASSERT(slots[3].introducer == derived && slots[3].method->mName == "Foo");

		VirtualTables::VptrLocation primary{ 0, derived };
		auto barOverrider = VirtualTables::FinalOverrider(derived, primary, slots[0]);
		TEST_ASSERT(barOverrider.owner == derived);
		auto drawIntOverrider = VirtualTables::FinalOverrider(derived, primary, slots[1]);
		TEST_ASSERT(drawIntOverrider.owner == base);
		auto drawFloatOverrider = VirtualTables::FinalOverrider(derived, primary, slots[2]);
		TEST_ASSERT(drawFloatOverrider.owner == derived);
	}

	void ObjectModelTest::EachInterfaceHasItsOwnVptr()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"interface IA { int32 A(); }\n"
			"interface IB { int32 B(); }\n"
			"class Base { public int32 x; public virtual int32 V() { return 0; } }\n"
			"class C : Base, IA, IB { public int32 A() { return 1; } public int32 B() { return 2; } }\n");
		const ObjectType* c = Object(model, "C");
		const ObjectType* ia = Object(model, "IA");
		const ObjectType* ib = Object(model, "IB");
		TEST_ASSERT(c && ia && ib);
		auto vptrs = VirtualTables::VptrLocations(c);
		TEST_ASSERT_EQ((int)vptrs.size(), 3);
		TEST_ASSERT(vptrs[0].offset == 0 && vptrs[0].layout == c);
		TEST_ASSERT(vptrs[1].layout == ia && vptrs[1].offset == (int64)c->GetBaseOffset(BaseIndex(c, ia)));
		TEST_ASSERT(vptrs[2].layout == ib && vptrs[2].offset == (int64)c->GetBaseOffset(BaseIndex(c, ib)));
		TEST_ASSERT(vptrs[1].offset != vptrs[2].offset);

		// The interface slot's final overrider is C's method, which expects the whole C (offset 0): the
		// vtable entry at the interface's vptr is a thunk that moves `this` back by the vptr's offset.
		auto ibSlots = VirtualTables::PrimarySlots(ib);
		TEST_ASSERT_EQ((int)ibSlots.size(), 1);
		auto ov = VirtualTables::FinalOverrider(c, vptrs[2], ibSlots[0]);
		TEST_ASSERT(ov.owner == c);
		TEST_ASSERT_EQ((int)ov.ownerOffset, 0);
	}

	void ObjectModelTest::InterfaceCallsDispatchThroughTheirVptr()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"interface IA { int32 A(); }\n"
			"interface IB { int32 B(); int32 B2(); }\n"
			"class C : IA, IB { public int32 A() { return 1; } public int32 B() { return 2; } public int32 B2() { return 3; } public int32 Plain() { return 4; } }\n");
		const ObjectType* c = Object(model, "C");
		const ObjectType* ib = Object(model, "IB");
		TEST_ASSERT(c && ib);
		auto viaInterface = VirtualTables::DispatchFor(c, ib, Method(ib, "B2"));
		TEST_ASSERT(viaInterface.isVirtual);
		TEST_ASSERT_EQ((int)viaInterface.vptrOffset, (int)c->GetBaseOffset(BaseIndex(c, ib)));
		TEST_ASSERT_EQ(viaInterface.index, 1);
		auto viaClass = VirtualTables::DispatchFor(c, c, Method(c, "B2"));
		TEST_ASSERT(viaClass.isVirtual);
		TEST_ASSERT_EQ((int)viaClass.vptrOffset, (int)viaInterface.vptrOffset);
		auto plain = VirtualTables::DispatchFor(c, c, Method(c, "Plain"));
		TEST_ASSERT(!plain.isVirtual);
	}

	// --- hierarchy ------------------------------------------------------------------

	void ObjectModelTest::RepeatedBaseIsAmbiguous()
	{
		// C4 rejects a repeated base in its own types, but a hierarchy built elsewhere (C++ RTTI) can have
		// one: every occurrence of the repeated type is marked ambiguous, the others are not.
		ObjectType iface(TypeKind::Object, "I");
		iface.SetObjectKind(ObjectType::ObjectKind::Interface);
		iface.SetDefined(true);
		ObjectType a(TypeKind::Object, "A");
		a.AddBaseType(&iface);
		a.SetDefined(true);
		ObjectType b(TypeKind::Object, "B");
		b.AddBaseType(&iface);
		b.SetDefined(true);
		ObjectType top(TypeKind::Object, "Top");
		top.AddBaseType(&a);
		top.AddBaseType(&b);
		top.SetDefined(true);
		auto list = ClassHierarchy::Subobjects(&top);
		TEST_ASSERT_EQ((int)list.size(), 5);
		int ambiguous = 0;
		for (const auto& s : list) ambiguous += s.ambiguous ? 1 : 0;
		TEST_ASSERT_EQ(ambiguous, 2);
		TEST_ASSERT(list[0].type == &top && list[0].containedBases == 4 && !list[0].ambiguous);
		TEST_ASSERT(list[1].type == &a && list[1].containedBases == 1);
		TEST_ASSERT(ClassHierarchy::LocateBase(&top, &iface).ambiguous);
		TEST_ASSERT(!ClassHierarchy::LocateBase(&top, &b).ambiguous);
	}

	// --- names and calling conventions ---------------------------------------------

	void ObjectModelTest::MsvcNamesSpellTypes()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"namespace Ns { class Foo { public int32 a; } struct Bar { public int32 b; } interface IBaz { int32 F(); } }\n"
			"template<typename T> struct Box { public T v; }\n"
			"template<int32 N> struct Arr { public int32[N] v; }\n"
			"alias IntBox = Box<int32>;\n"
			"alias FooBox = Box<Ns::Foo>;\n"
			"alias Arr3 = Arr<3>;\n");
		TypeStore* types = model.TypeStorePtr();
		const ObjectType* foo = Object(model, "Ns::Foo");
		TEST_ASSERT(foo != nullptr);
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(foo), std::string(".?AVFoo@Ns@@"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(Object(model, "Ns::Bar")), std::string(".?AUBar@Ns@@"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(Object(model, "Ns::IBaz")), std::string(".?AUIBaz@Ns@@"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(types->Builtin(TypeKind::Int32)), std::string(".H"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(types->Builtin(TypeKind::UInt64)), std::string("._K"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(types->Builtin(TypeKind::Bool)), std::string("._N"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(types->Builtin(TypeKind::Float64)), std::string(".N"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(types->Builtin(TypeKind::SNorm8)), std::string(".?AUsnorm8@__c4@@"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(types->Pointer(foo)), std::string(".PEAVFoo@Ns@@"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(types->Pointer(foo, true)), std::string(".PEAVFoo@Ns@@"));
		const Type* constIntPtr = types->Pointer(types->Builtin(TypeKind::Int32), true);
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(types->Pointer(constIntPtr)), std::string(".PEAPEBH"));
		TEST_ASSERT_EQ(MsvcNames::PointerTo(types->Builtin(TypeKind::Void)), std::string(".PEAX"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(Object(model, "IntBox")), std::string(".?AU?$Box@H@@"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(Object(model, "FooBox")), std::string(".?AU?$Box@VFoo@Ns@@@@"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(Object(model, "Arr3")), std::string(".?AU?$Arr@$02@@"));
	}

	void ObjectModelTest::MsvcNamesBackReferenceRepeatedNames()
	{
		// MSVC writes a repeated identifier as its index in the name's back-reference table; a template-id
		// has a table of its own and counts as one name of the enclosing one (what C++ catch clauses and
		// dynamic_cast compare).
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"namespace Ns { struct Foo { public int32 a; } template<T> struct Box { public T v; } }\n"
			"template<A, B> struct Pair { public A a; public B b; }\n"
			"template<T> struct Box { public T v; }\n"
			"alias Twin = Pair<Box<int32>, Box<int32>>;\n"
			"alias Nest = Box<Box<int32>>;\n"
			"alias InNs = Ns::Box<Ns::Foo>;\n"
			"alias SameScope = Pair<Ns::Foo, Ns::Foo>;\n");
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(Object(model, "Twin")), std::string(".?AU?$Pair@U?$Box@H@@U1@@@"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(Object(model, "Nest")), std::string(".?AU?$Box@U?$Box@H@@@@"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(Object(model, "InNs")), std::string(".?AU?$Box@UFoo@Ns@@@Ns@@"));
		TEST_ASSERT_EQ(MsvcNames::TypeDescriptorName(Object(model, "SameScope")), std::string(".?AU?$Pair@UFoo@Ns@@U12@@@"));
	}

	void ObjectModelTest::SignaturesFollowMsvcConventions()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"struct Small { public int64 v; }\n"
			"struct Large { public int64 a; public int64 b; public int64 c; }\n");
		const ObjectType* small = Object(model, "Small");
		const ObjectType* large = Object(model, "Large");
		TEST_ASSERT(small && large);
		llvm::LLVMContext ctx;
		Abi abi(ctx);

		// A member function: `this` first, then the result pointer; every object returns through it.
		auto member = abi.SignatureOf(small, Abi::CallKind::Member);
		TEST_ASSERT(member.sret);
		TEST_ASSERT_EQ(member.thisIndex, 0);
		TEST_ASSERT_EQ(member.sretIndex, 1);
		TEST_ASSERT_EQ((int)member.firstParam, 2);

		// An extension method is a C function whose receiver is its first argument: a small object
		// returns in a register, a large one through a result pointer that comes before the receiver.
		auto extSmall = abi.SignatureOf(small, Abi::CallKind::Extension);
		TEST_ASSERT(!extSmall.sret);
		TEST_ASSERT_EQ(extSmall.thisIndex, 0);
		TEST_ASSERT_EQ((int)extSmall.firstParam, 1);
		auto extLarge = abi.SignatureOf(large, Abi::CallKind::Extension);
		TEST_ASSERT_EQ(extLarge.sretIndex, 0);
		TEST_ASSERT_EQ(extLarge.thisIndex, 1);
		TEST_ASSERT_EQ((int)extLarge.firstParam, 2);

		auto freeLarge = abi.SignatureOf(large, Abi::CallKind::Free);
		TEST_ASSERT_EQ(freeLarge.sretIndex, 0);
		TEST_ASSERT_EQ(freeLarge.thisIndex, -1);
		TEST_ASSERT_EQ((int)freeLarge.firstParam, 1);
		auto freeSmall = abi.SignatureOf(small, Abi::CallKind::Free);
		TEST_ASSERT(!freeSmall.sret);
		TEST_ASSERT_EQ((int)freeSmall.firstParam, 0);
	}

	// --- emitted tables -------------------------------------------------------------

	void ObjectModelTest::VtablesThunksAndRttiAreEmitted()
	{
		CompilerInstance compiler;
		auto& model = MakeModel(compiler,
			"interface IA { int32 A(); }\n"
			"interface IB { int32 B(); }\n"
			"class Square : IA, IB { public int32 side; public int32 A() { return side; } public int32 B() { return side * 2; } }\n"
			"int32 main() { Square* s = new Square(); IB* b = s; IA* a = b as IA*; return a->A() + b->B(); }\n");
		llvm::LLVMContext ctx;
		CodeGenDriver driver(ctx);
		std::string error;
		TEST_ASSERT(driver.Lower(model, "main", &error));
		llvm::Module* m = driver.module();
		bool thunk = false;
		for (const llvm::Function& f : *m)
			if (f.getName().starts_with("c4.thunk.")) thunk = true;
		TEST_ASSERT(thunk);                                                    // IB's entry moves `this` to the Square
		TEST_ASSERT(m->getGlobalVariable("??_R0?AVSquare@@@8") != nullptr);   // TypeDescriptor with the MSVC name
		TEST_ASSERT(m->getGlobalVariable("??_R0?AUIA@@@8") != nullptr);
		TEST_ASSERT(m->getFunction("c4_dynamic_cast") != nullptr);            // the cross-cast reads the RTTI
		int vtables = 0;
		for (const llvm::GlobalVariable& g : m->globals())
			if (g.getName().starts_with("__c4_vtable_Square_")) vtables++;
		TEST_ASSERT_EQ(vtables, 2);                                            // one per vptr: IA (primary) and IB
	}
} // namespace Testing
