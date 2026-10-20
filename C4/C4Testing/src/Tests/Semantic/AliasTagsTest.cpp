#include "AliasTagsTest.h"
#include "Backend/CodeGen/CodeGen.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/Toolchain.h"
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <functional>
#include <memory>
#include <set>
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		using Classes = std::set<std::string>;

		// A lowered program: its module (which outlives the driver), its text, or the lowering's errors.
		struct Lowered
		{
			std::unique_ptr<llvm::LLVMContext> context = std::make_unique<llvm::LLVMContext>();
			std::unique_ptr<llvm::Module> module;
			std::string ir;
			std::string errors;
		};

		Lowered Lower(const char* source, bool strictAliasing = true)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("t.cf", source);
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			Lowered out;
			CodeGenDriver driver(*out.context);
			if (!strictAliasing) driver.DisableStrictAliasing();
			std::string error;
			const bool ok = driver.Lower(compiler.GetProgramModelForLowering(), "main", &error);
			for (const CompilationError& e : driver.LoweringErrors()) out.errors += std::string(e.message.c_str()) + "\n";
			out.errors += error;
			if (!ok) return out;
			out.module = driver.TakeModule();
			llvm::raw_string_ostream text(out.ir);
			out.module->print(text, nullptr);
			return out;
		}

		// The function whose name starts with `prefix` (the program's main is `main_0`).
		const llvm::Function* FunctionNamed(const Lowered& lowered, const char* prefix)
		{
			if (!lowered.module) return nullptr;
			for (const llvm::Function& fn : *lowered.module)
				if (!fn.isDeclaration() && fn.getName().starts_with(prefix)) return &fn;
			return nullptr;
		}

		// The access type an access is tagged with, or "" when it carries no tag.
		std::string ClassOf(const llvm::Instruction& access)
		{
			const llvm::MDNode* tag = access.getMetadata(llvm::LLVMContext::MD_tbaa);
			if (!tag || tag->getNumOperands() < 2) return "";
			const auto* type = llvm::dyn_cast<llvm::MDNode>(tag->getOperand(1));
			const auto* name = type && type->getNumOperands() ? llvm::dyn_cast<llvm::MDString>(type->getOperand(0)) : nullptr;
			return name ? name->getString().str() : "";
		}

		Classes ClassesOf(const llvm::Function* fn, const std::function<bool(const llvm::Instruction&)>& wanted)
		{
			Classes classes;
			if (!fn) return classes;
			for (const llvm::Instruction& inst : llvm::instructions(*fn))
				if (wanted(inst)) classes.insert(ClassOf(inst));
			return classes;
		}

		// Whether `address` is `global` plus `offset` bytes; a negative offset accepts any offset, constant or not.
		bool At(const llvm::Value* address, const llvm::DataLayout& layout, const char* global, int64_t offset)
		{
			if (offset < 0) return address->stripInBoundsOffsets()->getName() == global;
			llvm::APInt at(64, 0);
			const llvm::Value* base = address->stripAndAccumulateConstantOffsets(layout, at, true);
			return base->getName() == global && at.getSExtValue() == offset;
		}

		Classes StoresTo(const Lowered& lowered, const char* global, int64_t offset = 0)
		{
			const llvm::DataLayout& layout = lowered.module->getDataLayout();
			return ClassesOf(FunctionNamed(lowered, "main_0"), [&](const llvm::Instruction& inst) {
				const auto* store = llvm::dyn_cast<llvm::StoreInst>(&inst);
				return store && At(store->getPointerOperand(), layout, global, offset);
			});
		}

		Classes LoadsFrom(const Lowered& lowered, const char* global)
		{
			const llvm::DataLayout& layout = lowered.module->getDataLayout();
			return ClassesOf(FunctionNamed(lowered, "main_0"), [&](const llvm::Instruction& inst) {
				const auto* load = llvm::dyn_cast<llvm::LoadInst>(&inst);
				return load && At(load->getPointerOperand(), layout, global, -1);
			});
		}

		Classes CallsTo(const llvm::Function* fn, const char* callee)
		{
			return ClassesOf(fn, [&](const llvm::Instruction& inst) {
				const auto* call = llvm::dyn_cast<llvm::CallBase>(&inst);
				return call && call->getCalledFunction() && call->getCalledFunction()->getName().starts_with(callee);
			});
		}

		bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }
	}

	void AliasTagsTest::ScalarsTakeTheClassOfTheirSize()
	{
		const Lowered lowered = Lower(R"(
			enum class Small : uint16 { A, B }
			bool g_bool; int8 g_i8; uint8 g_u8; char8 g_c8; int16 g_i16; char16 g_c16; Small g_small; int32 g_i32; uint32 g_u32;
			int64 g_i64; uint64 g_u64; float g_f; double g_d; int32* g_p; funcptr<int32()> g_fp;
			int32 Seven() { return 7; }
			int32 main() {
				g_bool = true; g_i8 = 1; g_u8 = 2; g_c8 = 'a'; g_i16 = 3; g_c16 = (char16)4; g_small = Small::B; g_i32 = 5; g_u32 = 6u;
				g_i64 = 7; g_u64 = 8u; g_f = 1.0f; g_d = 2.0; g_p = &g_i32; g_fp = Seven;
				return 0;
			})");
		TEST_ASSERT(lowered.module);
		TEST_ASSERT(StoresTo(lowered, "g_bool") == Classes{ "bool" });
		for (const char* bytes : { "g_i8", "g_u8", "g_c8" }) TEST_ASSERT(StoresTo(lowered, bytes) == Classes{ "bytes" });
		for (const char* half : { "g_i16", "g_c16", "g_small" }) TEST_ASSERT(StoresTo(lowered, half) == Classes{ "int16" });
		TEST_ASSERT(StoresTo(lowered, "g_i32") == Classes{ "int32" } && StoresTo(lowered, "g_u32") == Classes{ "int32" });
		TEST_ASSERT(StoresTo(lowered, "g_i64") == Classes{ "int64" } && StoresTo(lowered, "g_u64") == Classes{ "int64" });
		TEST_ASSERT(StoresTo(lowered, "g_f") == Classes{ "float" } && StoresTo(lowered, "g_d") == Classes{ "double" });
		TEST_ASSERT(StoresTo(lowered, "g_p") == Classes{ "pointer" } && StoresTo(lowered, "g_fp") == Classes{ "pointer" });
		TEST_ASSERT(Has(lowered.ir, "!{!\"C4 TBAA\"}"));
		TEST_PASSED();
	}

	void AliasTagsTest::ObjectsTakeTheirFieldsClass()
	{
		const Lowered lowered = Lower(R"(
			struct Floats { public float a; public packed float[8] b; public float[3] c; }
			struct Mixed { public float a; public int32 b; }
			class Base { public float x; }
			class Child : Base { public float y; }
			class Virtual { public float a; public virtual int32 F() { return 1; } }
			struct Holder { public Floats inner; public double d; }
			Floats g_floats; Mixed g_mixed; Child g_child; Virtual g_virtual; Holder g_holder;
			int32 main() {
				Floats f; g_floats = f;
				Mixed m; g_mixed = m;
				Child c; g_child = c;
				Virtual v; g_virtual = v;
				Holder h; g_holder = h;
				return 0;
			})");
		TEST_ASSERT(lowered.module);
		TEST_ASSERT(StoresTo(lowered, "g_floats") == Classes{ "float" });
		TEST_ASSERT(StoresTo(lowered, "g_child") == Classes{ "float" });
		TEST_ASSERT(StoresTo(lowered, "g_mixed") == Classes{ "bytes" });
		TEST_ASSERT(StoresTo(lowered, "g_holder") == Classes{ "bytes" });
		TEST_ASSERT(StoresTo(lowered, "g_virtual").count("bytes") == 1);   // its vptr is a pointer; the vptr store has no tag
		TEST_PASSED();
	}

	void AliasTagsTest::UnionMembersOfSeveralClassesAreBytes()
	{
		const Lowered lowered = Lower(R"(
			union U { float f; int32 i; }
			union V { float a; float[2] b; }
			struct S { public union { float f; uint32 bits; }; public float g; public union { float x; float y; }; }
			U g_u; V g_v; S g_s;
			int32 main() {
				g_u.f = 1.0f; g_v.a = 1.0f; g_s.f = 1.0f; g_s.g = 2.0f; g_s.x = 3.0f;
				return g_u.i;
			})");
		TEST_ASSERT(lowered.module);
		TEST_ASSERT(StoresTo(lowered, "g_u") == Classes{ "bytes" } && LoadsFrom(lowered, "g_u") == Classes{ "bytes" });
		TEST_ASSERT(StoresTo(lowered, "g_v") == Classes{ "float" });   // every member is a float
		TEST_ASSERT(StoresTo(lowered, "g_s", 0) == Classes{ "bytes" });
		TEST_ASSERT(StoresTo(lowered, "g_s", 4) == Classes{ "float" });
		TEST_ASSERT(StoresTo(lowered, "g_s", 8) == Classes{ "float" });
		TEST_PASSED();
	}

	void AliasTagsTest::AnElementOfAUnionArrayIsBytes()
	{
		const Lowered lowered = Lower(R"(
			union W { float[4] a; int32 i; }
			W g_w;
			float[4] g_plain;
			volatile int32 g_k = 2;
			int32 main() {
				g_w.a[g_k] = 1.0f;
				g_plain[g_k] = 2.0f;
				return 0;
			})");
		TEST_ASSERT(lowered.module);
		TEST_ASSERT(StoresTo(lowered, "g_w", -1) == Classes{ "bytes" });
		TEST_ASSERT(StoresTo(lowered, "g_plain", -1) == Classes{ "float" });
		TEST_PASSED();
	}

	void AliasTagsTest::PackedLanesTakeTheirElementsClass()
	{
		const Lowered lowered = Lower(R"(
			struct Vec4 { public union { packed float x, y, z, w; packed float[4] data; }; }
			Vec4 g_vec;
			volatile int32 g_k = 1;
			int32 main() {
				g_vec.y = 1.0f;
				g_vec.data[g_k] = 2.0f;
				g_vec.zw = g_vec.xy;
				return 0;
			})");
		TEST_ASSERT(lowered.module);
		TEST_ASSERT(StoresTo(lowered, "g_vec", -1) == Classes{ "float" });
		TEST_ASSERT(LoadsFrom(lowered, "g_vec") == Classes{ "float" });
		TEST_PASSED();
	}

	void AliasTagsTest::SimdMemoryTakesTheLanesClass()
	{
		const Lowered lowered = Lower(R"(
			float[8] g_f;
			int32[8] g_i;
			int32 main() {
				packed float[8] v = simd_load(&g_f[0], 8);
				simd_store(&g_i[0], (packed int32[8])v);
				simd_store_masked(&g_f[0], v, v > 0.0f);
				return 0;
			})");
		TEST_ASSERT(lowered.module);
		TEST_ASSERT(LoadsFrom(lowered, "g_f") == Classes{ "float" });
		TEST_ASSERT(StoresTo(lowered, "g_i", -1) == Classes{ "int32" });
		TEST_ASSERT(CallsTo(FunctionNamed(lowered, "main_0"), "llvm.masked.store") == Classes{ "float" });
		TEST_PASSED();
	}

	void AliasTagsTest::PointersInAStructArePointers()
	{
		const Lowered lowered = Lower(R"(
			struct View { public float* out; public const float* in; public int32 count; }
			void Scale(View& view) { for (int32 i = 0; i < view.count; ++i) view.out[i] = view.in[i] * 2.0f; }
			int32 main() { View v; Scale(v); return 0; }
			)");
		TEST_ASSERT(lowered.module);
		const llvm::Function* scale = FunctionNamed(lowered, "Scale");
		TEST_ASSERT(scale);
		auto loadsOf = [&](auto isType) {
			return ClassesOf(scale, [&](const llvm::Instruction& inst) { return llvm::isa<llvm::LoadInst>(inst) && isType(inst.getType()); });
		};
		TEST_ASSERT(loadsOf([](const llvm::Type* t) { return t->isPointerTy(); }) == Classes{ "pointer" });
		TEST_ASSERT(loadsOf([](const llvm::Type* t) { return t->isIntegerTy(32); }) == Classes{ "int32" });
		TEST_ASSERT(loadsOf([](const llvm::Type* t) { return t->isFloatTy(); }) == Classes{ "float" });
		TEST_ASSERT(ClassesOf(scale, [](const llvm::Instruction& inst) {
			return llvm::isa<llvm::StoreInst>(inst) && llvm::cast<llvm::StoreInst>(inst).getValueOperand()->getType()->isFloatTy();
		}) == Classes{ "float" });
		TEST_PASSED();
	}

	void AliasTagsTest::NoStrictAliasingTagsNothing()
	{
		const char* source = R"(
			union U { float f; int32 i; }
			U g_u;
			float g_f;
			int32 main() { g_u.f = 1.0f; g_f = 2.0f; return g_u.i; }
			)";
		const Lowered strict = Lower(source);
		const Lowered loose = Lower(source, false);
		TEST_ASSERT(strict.module && loose.module);
		TEST_ASSERT(Has(strict.ir, "!tbaa"));
		TEST_ASSERT(!Has(loose.ir, "!tbaa") && !Has(loose.ir, "C4 TBAA"));
		TEST_ASSERT(StoresTo(loose, "g_f") == Classes{ "" });
		TEST_PASSED();
	}

	void AliasTagsTest::ReinterpretCastKeepsANumbersBits()
	{
		const Lowered lowered = Lower(R"(
			int32 Bits(float f) { return reinterpret_cast<int32>(f); }
			double Real(int64 i) { return reinterpret_cast<double>(i); }
			uint32 Unsigned(int32 i) { return reinterpret_cast<uint32>(i); }
			int32 Converted(float f) { return (int32)f; }
			int32 main() { return Bits(1.0f) + (int32)Real(1) + (int32)Unsigned(-1) + Converted(2.5f); }
			)");
		TEST_ASSERT(lowered.module);
		TEST_ASSERT(Has(lowered.ir, "bitcast float"));
		TEST_ASSERT(Has(lowered.ir, "bitcast i64"));
		TEST_ASSERT(Has(lowered.ir, "fptosi float"));
		const llvm::Function* same = FunctionNamed(lowered, "Unsigned");
		TEST_ASSERT(same && ClassesOf(same, [](const llvm::Instruction& inst) { return llvm::isa<llvm::CastInst>(inst); }).empty());
		TEST_PASSED();
	}
} // namespace Testing
