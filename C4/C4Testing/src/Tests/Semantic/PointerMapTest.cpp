#include "PointerMapTest.h"
#include "Backend/ProgramBackend.h"
#include "Backend/HIR/CountedArray.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/Toolchain.h"
#include <algorithm>
#include <map>
#include <string>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/LLVMContext.h>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// A c4_gc_type record as the module holds it: { name, counter, pointers, words, array, firstElementWords }.
		struct Record
		{
			std::vector<uint64_t> pointers;   // empty: no map (read whole)
			uint32_t words = 0;
			uint16_t array = 0;
			uint16_t firstElementWords = 0;
		};

		std::string NameOf(const llvm::ConstantStruct& record)
		{
			const auto* text = llvm::dyn_cast<llvm::GlobalVariable>(record.getOperand(0));
			const auto* bytes = text && text->hasInitializer() ? llvm::dyn_cast<llvm::ConstantDataSequential>(text->getInitializer()) : nullptr;
			return bytes ? bytes->getAsCString().str() : std::string();
		}

		uint64_t IntOf(const llvm::Constant* value)
		{
			const auto* number = llvm::dyn_cast<llvm::ConstantInt>(value);
			return number ? number->getZExtValue() : ~uint64_t(0);
		}

		// The words of the c4.pointer_map constant `pointer` names; none for a null one.
		std::vector<uint64_t> MapAt(const llvm::Constant* pointer)
		{
			std::vector<uint64_t> words;
			if (const auto* map = llvm::dyn_cast<llvm::GlobalVariable>(pointer))
				if (const auto* data = map->hasInitializer() ? llvm::dyn_cast<llvm::ConstantDataSequential>(map->getInitializer()) : nullptr)
					for (unsigned i = 0; i < data->getNumElements(); ++i) words.push_back(data->getElementAsInteger(i));
			return words;
		}

		Record Read(const llvm::ConstantStruct& record)
		{
			Record read;
			read.pointers = MapAt(record.getOperand(2));
			read.words = static_cast<uint32_t>(IntOf(record.getOperand(3)));
			read.array = static_cast<uint16_t>(IntOf(record.getOperand(4)));
			read.firstElementWords = static_cast<uint16_t>(IntOf(record.getOperand(5)));
			return read;
		}

		// Lowers the program and hands its module to `inspect` while it is alive.
		template <typename Inspect>
		bool Lower(const char* source, Inspect inspect)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("t.cf", source);
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			llvm::LLVMContext ctx;
			ProgramBackend backend(ctx);
			if (!backend.Lower(compiler.GetProgramModelForLowering(), "main", nullptr, nullptr)) return false;
			inspect(*backend.module());
			return true;
		}

		// Every c4_gc_type record of the program's module, by name.
		std::map<std::string, Record> RecordsOf(const char* source)
		{
			std::map<std::string, Record> records;
			Lower(source, [&](llvm::Module& module) {
				for (const llvm::GlobalVariable& global : module.globals())
					if (global.getName().starts_with("c4.census_type") && global.hasInitializer())
						if (const auto* record = llvm::dyn_cast<llvm::ConstantStruct>(global.getInitializer()))
							if (record->getNumOperands() == 6) records[NameOf(*record)] = Read(*record);
			});
			return records;
		}

		// Every root __c4_gc_roots lists: its storage's name and its map (empty: read whole).
		std::map<std::string, std::vector<uint64_t>> RootsOf(const char* source)
		{
			std::map<std::string, std::vector<uint64_t>> roots;
			Lower(source, [&](llvm::Module& module) {
				const llvm::GlobalVariable* table = module.getGlobalVariable("__c4_gc_roots");
				const auto* entries = table && table->hasInitializer() ? llvm::dyn_cast<llvm::ConstantArray>(table->getInitializer()) : nullptr;
				for (unsigned i = 0; entries && i + 1 < entries->getNumOperands(); ++i)
				{
					const auto* range = llvm::cast<llvm::ConstantStruct>(entries->getOperand(i));
					roots[range->getOperand(0)->stripPointerCasts()->getName().str()] = MapAt(range->getOperand(2));
				}
			});
			return roots;
		}
	}

	void PointerMapTest::ARecordMapsTheWordsThatMayHoldAPointer()
	{
		// Word by word: an integer and a float are no pointer, a pointer field is; a vptr points at the program's tables,
		// not the heap; a base's fields sit at its offset; a union's word is one when a member may be; a func holds its
		// environment.
		const auto records = RecordsOf(
			"struct Mixed { public int64 a; public Mixed* next; public float f; }\n"
			"class Base { public Mixed* m; public virtual int32 Kind() { return 1; } }\n"
			"class Derived : Base { public int64 x; public override int32 Kind() { return 2; } }\n"
			"union Word { public Mixed* p; public int64 i; }\n"
			"struct Punned { public int64 a; public Word w; public int32[4] n; }\n"
			"struct Calls { public int64 a; public func<int32()> f; }\n"
			"int32 main() { Mixed* m = new Mixed; Derived* d = new Derived; Punned* p = new Punned; Calls* c = new Calls;\n"
			"  return d->Kind() + (int32)m->a + (int32)p->a + (int32)c->a; }");
		TEST_ASSERT(records.count("Mixed") && records.count("Derived") && records.count("Punned") && records.count("Calls"));
		const Record& mixed = records.at("Mixed");
		TEST_ASSERT_EQ((int)mixed.words, 3);
		TEST_ASSERT(mixed.pointers.size() == 1 && mixed.pointers[0] == 0b010);
		TEST_ASSERT_EQ((int)mixed.array, 0);
		const Record& derived = records.at("Derived");
		TEST_ASSERT_EQ((int)derived.words, 3);
		TEST_ASSERT(derived.pointers.size() == 1 && derived.pointers[0] == 0b010);
		const Record& punned = records.at("Punned");
		TEST_ASSERT_EQ((int)punned.words, 4);
		TEST_ASSERT(punned.pointers.size() == 1 && punned.pointers[0] == 0b0010);
		const Record& calls = records.at("Calls");
		TEST_ASSERT_EQ((int)calls.words, 3);
		TEST_ASSERT(calls.pointers.size() == 1 && calls.pointers[0] == 0b110);
		TEST_PASSED();
	}

	void PointerMapTest::AnArrayRecordMapsOneElementAfterItsCount()
	{
		// `new T[n]`: the element's map, repeated by the collector; an element with a finalizer makes the array keep its
		// count in front of the first element, which the record says.
		const auto records = RecordsOf(
			"int32 g_ended = 0;\n"
			"struct Mixed { public int64 a; public Mixed* next; }\n"
			"struct Held { public int64 a; public Mixed* m; [operator(\"delete\")] void End() { g_ended++; } }\n"
			"int32 main() { int32 n = 4; Mixed* plain = new Mixed[n]; Held* held = new Held[n]; return (int32)plain[1].a + (int32)held[1].a; }");
		TEST_ASSERT(records.count("Mixed[]") && records.count("Held[]"));
		const Record& plain = records.at("Mixed[]");
		TEST_ASSERT_EQ((int)plain.array, 1);
		TEST_ASSERT_EQ((int)plain.words, 2);
		TEST_ASSERT(plain.pointers.size() == 1 && plain.pointers[0] == 0b10);
		TEST_ASSERT_EQ((int)plain.firstElementWords, 0);
		const Record& held = records.at("Held[]");
		TEST_ASSERT_EQ((int)held.array, 1);
		TEST_ASSERT_EQ((int)held.firstElementWords * 8, (int)CountedArray::PrefixBytes(8));
		TEST_ASSERT(held.pointers.size() == 1 && held.pointers[0] == 0b10);
		TEST_PASSED();
	}

	void PointerMapTest::ATypeOfPointersOnlyIsReadWhole()
	{
		// A map with every word set reads what a whole scan reads: the record has none, and the object is allocated by
		// c4_alloc_typed and scanned whole; a type with a map is allocated by c4_alloc_precise.
		const char* source =
			"struct Node { public Node* left; public Node* right; }\n"
			"struct Mixed { public int64 a; public Node* n; }\n"
			"Node* MakeNode() { return new Node; }\n"
			"Mixed* MakeMixed() { return new Mixed; }\n"
			"int32 main() { Node** many = new Node*[3]; return MakeNode()->left == null && MakeMixed()->a == 0 && many[0] == null ? 0 : 1; }";
		const auto records = RecordsOf(source);
		TEST_ASSERT(records.count("Node") && records.count("Node*[]"));
		TEST_ASSERT(records.at("Node").pointers.empty());
		TEST_ASSERT_EQ((int)records.at("Node").words, 0);
		TEST_ASSERT(records.at("Node*[]").pointers.empty());
		std::map<std::string, std::vector<std::string>> callees;
		TEST_ASSERT(Lower(source, [&](llvm::Module& module) {
			for (const llvm::Function& f : module)
				for (const char* maker : { "MakeNode", "MakeMixed" })
					if (f.getName().starts_with(maker) && !f.isDeclaration())
						for (const llvm::BasicBlock& block : f)
							for (const llvm::Instruction& inst : block)
								if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&inst))
									if (const llvm::Function* callee = call->getCalledFunction()) callees[maker].push_back(callee->getName().str());
		}));
		auto calls = [&](const char* maker, const char* name) { return std::find(callees[maker].begin(), callees[maker].end(), name) != callees[maker].end(); };
		TEST_ASSERT(calls("MakeNode", "c4_alloc_typed") && !calls("MakeNode", "c4_alloc_precise"));
		TEST_ASSERT(calls("MakeMixed", "c4_alloc_precise") && !calls("MakeMixed", "c4_alloc_typed"));
		TEST_PASSED();
	}

	void PointerMapTest::GcTypeOfIsTheTypesMapRecord()
	{
		// gc_type_of(T) (B70): T's c4_gc_type for a scan region of T elements - its map, and no census counter even for a
		// type that holds native memory, so lowering plans nothing for it.
		std::vector<std::string> counters;
		const char* source =
			"struct Buffer { public int64 bytes; [operator(\"native_bytes\")] public int64 NativeBytes() const { return bytes; } }\n"
			"struct Mixed { public int64 a; public Mixed* next; public Buffer buffer; }\n"
			"int32 main() { const void* t = gc_type_of(Mixed); return t == null ? 1 : 0; }";
		const auto records = RecordsOf(source);
		TEST_ASSERT(records.count("Mixed"));
		TEST_ASSERT(records.at("Mixed").pointers.size() == 1 && records.at("Mixed").pointers[0] == 0b010);
		TEST_ASSERT_EQ((int)records.at("Mixed").words, 3);
		TEST_ASSERT(Lower(source, [&](llvm::Module& module) {
			for (const llvm::GlobalVariable& global : module.globals())
				if (global.getName().starts_with("c4.census_type") && global.hasInitializer())
					if (const auto* record = llvm::dyn_cast<llvm::ConstantStruct>(global.getInitializer()))
						if (NameOf(*record) == "Mixed") counters.push_back(llvm::isa<llvm::ConstantPointerNull>(record->getOperand(1)) ? "null" : "counter");
		}));
		TEST_ASSERT(counters.size() == 1 && counters[0] == "null");
		TEST_PASSED();
	}

	void PointerMapTest::AGlobalRootCarriesItsTypesMap()
	{
		// __c4_gc_roots gives each root its type's map: the collector reads a global holder's pointer word, not its numbers;
		// an array of pointers has none and is read whole.
		const auto roots = RootsOf(
			"struct Node { public int32 v; }\n"
			"struct Holder { public int64[3] numbers; public Node* item; }\n"
			"Holder g_holder;\n"
			"Node*[4] g_array;\n"
			"int32 main() { return g_holder.item == null && g_array[0] == null ? 0 : 1; }");
		std::string holder, array;
		for (const auto& [name, map] : roots)
		{
			if (name.find("g_holder") != std::string::npos) holder = name;
			if (name.find("g_array") != std::string::npos) array = name;
		}
		TEST_ASSERT(!holder.empty() && !array.empty());
		TEST_ASSERT(roots.at(holder).size() == 1 && roots.at(holder)[0] == 0b1000);
		TEST_ASSERT(roots.at(array).empty());
		TEST_PASSED();
	}
} // namespace Testing
