#include "StackMapsTest.h"
#include "Backend/ProgramBackend.h"
#include "Compiler/BuildUtils.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/ObjectEmission.h"
#include "Compiler/RuntimeObjects.h"
#include "Compiler/StackMapFormat.h"
#include "Compiler/StackMapRecords.h"
#include "Compiler/StackMaps.h"
#include "Compiler/StackMapValues.h"
#include "Compiler/Toolchain.h"
#include <llvm/AsmParser/Parser.h>
#include <llvm/BinaryFormat/ELF.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Object/ELFObjectFile.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		constexpr llvm::CodeGenOptLevel kOptimized = llvm::CodeGenOptLevel::Default;

		// `a`, `b` and `c` are read after the third `new`, only `c` after Call, which may collect (it allocates): at -O0
		// each is a stack slot.
		constexpr const char* kProgram = R"(
			struct T { public int32 v; }
			int32 g_calls = 0;
			void Call() { T* made = new T; g_calls += made->v + 1; }
			void Use(T* t) { t->v++; }
			int32 Slots() {
				T* a = new T;
				T* b = new T;
				T* c = new T;
				a->v = b->v + c->v;
				Call();
				return c->v;
			}
			int32 main() {
				T* t = new T;
				Use(t);
				return Slots();
			}
		)";

		struct Lowered
		{
			std::unique_ptr<llvm::LLVMContext> context = std::make_unique<llvm::LLVMContext>();
			std::unique_ptr<llvm::Module> module;
			std::string ir;
			std::string errors;
		};

		std::string Text(const llvm::Module& module)
		{
			std::string text;
			llvm::raw_string_ostream out(text);
			module.print(out, nullptr);
			return text;
		}

		// A type as IR writes it: two modules' types are compared by it (each module has a context of its own).
		std::string Text(const llvm::Type* type)
		{
			std::string text;
			llvm::raw_string_ostream out(text);
			type->print(out);
			return text;
		}

		Lowered Lower(const char* source, bool stackMaps, TargetSystem system = HostSystem())
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("t.cf", source);
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			Lowered out;
			ProgramBackend driver(*out.context, system);
			if (stackMaps) driver.UseStackMaps();
			std::string error;
			const bool ok = driver.Lower(compiler.GetProgramModelForLowering(), "main", &error);
			for (const CompilationError& e : driver.LoweringErrors()) out.errors += std::string(e.message.c_str()) + "\n";
			out.errors += error;
			if (!ok) return out;
			out.module = driver.TakeModule();
			out.ir = Text(*out.module);
			return out;
		}

		// A module written as IR, for a target, marked for stack maps.
		std::unique_ptr<llvm::Module> Parsed(llvm::LLVMContext& context, const std::string& body, TargetSystem system = TargetSystem::Windows)
		{
			const char* triple = system == TargetSystem::Linux ? "x86_64-unknown-linux-gnu" : "x86_64-pc-windows-msvc";
			llvm::SMDiagnostic error;
			std::unique_ptr<llvm::Module> module = llvm::parseAssemblyString(std::string("target triple = \"") + triple + "\"\n" + body, error, context);
			if (!module)
			{
				std::string message;
				llvm::raw_string_ostream out(message);
				error.print("stack maps test", out);
				printf("%s\n", message.c_str());
				return nullptr;
			}
			MarkForStackMaps(*module);
			return module;
		}

		const llvm::Function* FunctionNamed(const llvm::Module& module, const char* prefix)
		{
			for (const llvm::Function& fn : module)
				if (!fn.isDeclaration() && fn.getName().starts_with(prefix)) return &fn;
			return nullptr;
		}

		bool Verified(const llvm::Module& module)
		{
			std::string problems;
			llvm::raw_string_ostream out(problems);
			const bool broken = llvm::verifyModule(module, &out);
			if (broken) printf("%s\n", problems.c_str());
			return !broken;
		}

		// The calls of `fn` with a record (a deopt list) whose callee's name starts with `prefix`.
		std::vector<const llvm::CallBase*> RecordedCalls(const llvm::Function& fn, const char* prefix)
		{
			std::vector<const llvm::CallBase*> found;
			for (const llvm::Instruction& instruction : llvm::instructions(fn))
				if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction))
					if (const llvm::Function* callee = call->getCalledFunction(); callee && callee->getName().starts_with(prefix) && call->getOperandBundle(llvm::LLVMContext::OB_deopt))
						found.push_back(call);
			return found;
		}

		// How many stack slots a call lists (Compiler/StackMapSlots: each slot, then its words and masks).
		size_t ListedSlots(const llvm::CallBase& call)
		{
			const std::optional<llvm::OperandBundleUse> deopt = call.getOperandBundle(llvm::LLVMContext::OB_deopt);
			size_t slots = 0;
			if (deopt)
				for (const llvm::Use& input : deopt->Inputs) slots += llvm::isa<llvm::AllocaInst>(input.get()) ? 1 : 0;
			return slots;
		}

		// The names of the values ValuesLiveAcross gives for the call of `callee` in `fn`.
		std::vector<std::string> LiveAcross(llvm::Function& fn, const char* callee)
		{
			std::vector<llvm::CallBase*> calls;
			for (llvm::Instruction& instruction : llvm::instructions(fn))
				if (auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction); call && call->getCalledFunction() && call->getCalledFunction()->getName() == callee)
					calls.push_back(call);
			std::vector<std::string> names;
			if (calls.size() != 1) return names;
			const auto live = ValuesLiveAcross(fn, calls);
			const auto found = live.find(calls[0]);
			if (found != live.end())
				for (const llvm::Value* value : found->second) names.push_back(value->getName().str());
			return names;
		}

		// The object code generation writes for `module` and `system`; nullopt when it writes none.
		std::optional<std::string> ObjectOf(llvm::Module& module, TargetSystem system, int optLevel)
		{
			const CpuTarget target = CpuTarget().On(system);
			const ObjectSettings settings{ optLevel, &target, 1, false };
			const std::string path = (std::filesystem::temp_directory_path() / "c4_stack_maps_test_object.o").string();
			std::vector<ObjectDiagnostic> diagnostics;
			const bool written = WriteObject(module, settings, path, diagnostics);
			for (const ObjectDiagnostic& diagnostic : diagnostics) printf("%s\n", diagnostic.message.c_str());
			std::ostringstream bytes;
			bytes << std::ifstream(path, std::ios::binary).rdbuf();
			std::error_code ec;
			std::filesystem::remove(path, ec);
			if (!written || !diagnostics.empty()) return std::nullopt;
			return bytes.str();
		}

		bool ObjectWritten(llvm::Module& module, TargetSystem system, int optLevel) { return ObjectOf(module, system, optLevel).has_value(); }

		// The section `name` of `object`, nullopt when it has none.
		std::optional<llvm::object::SectionRef> SectionNamed(const llvm::object::ObjectFile& object, llvm::StringRef name)
		{
			for (const llvm::object::SectionRef& section : object.sections())
			{
				llvm::Expected<llvm::StringRef> found = section.getName();
				if (!found)
				{
					llvm::consumeError(found.takeError());
					continue;
				}
				if (*found == name) return section;
			}
			return std::nullopt;
		}

		// What a stack map (Compiler/StackMapFormat) says of a record: its return address's offset in its function, how many
		// values it names, how many slots it lists.
		struct MapRecord
		{
			uint32_t offset = 0;
			bool whole = false;
			size_t values = 0;
			size_t slots = 0;
		};

		uint64_t Uleb(const uint8_t*& at)
		{
			uint64_t value = 0;
			for (unsigned shift = 0;; shift += 7)
			{
				const uint8_t byte = *at++;
				value |= static_cast<uint64_t>(byte & 0x7f) << shift;
				if (!(byte & 0x80)) return value;
			}
		}

		uint32_t Read32(const uint8_t* at)
		{
			uint32_t value = 0;
			std::memcpy(&value, at, sizeof value);
			return value;
		}

		// The records of each function of one object's map; empty when `map` does not start with a map of this format.
		std::vector<std::vector<MapRecord>> RecordsOf(llvm::StringRef map)
		{
			constexpr size_t c_HeaderBytes = 16, c_AddressBytes = 8, c_OffsetBytes = 4;
			std::vector<std::vector<MapRecord>> functions;
			const uint8_t* at = reinterpret_cast<const uint8_t*>(map.data());
			if (map.size() < c_HeaderBytes || Read32(at) != c_StackMapMagic || Read32(at + 4) != c_StackMapVersion || Read32(at + 12) > map.size()) return functions;
			const uint32_t count = Read32(at + 8);
			at += c_HeaderBytes + count * c_AddressBytes;
			for (uint32_t f = 0; f < count; ++f)
			{
				const uint64_t records = Uleb(at);
				const uint64_t slots = Uleb(at);
				for (uint64_t s = 0; s < slots; ++s)
				{
					Uleb(at);
					Uleb(at); // a SLEB is skipped as a ULEB
					const uint64_t words = Uleb(at);
					if (!(words & 1))
						for (uint64_t first = 0; first < (words >> 1); first += 32) Uleb(at);
				}
				std::vector<MapRecord>& list = functions.emplace_back();
				for (uint64_t r = 0; r < records; ++r)
				{
					MapRecord record;
					record.offset = Read32(at);
					at += c_OffsetBytes;
					const uint64_t values = Uleb(at);
					record.whole = (values & 1) != 0;
					record.values = values >> 1;
					if (!record.whole)
					{
						for (size_t v = 0; v < record.values; ++v)
							if (Uleb(at) & 1) Uleb(at);
						for (uint64_t s = 0; s < slots; ++s) record.slots += (at[s / 8] >> (s % 8)) & 1;
						at += (slots + 7) / 8;
					}
					list.push_back(record);
				}
			}
			return functions;
		}

		// The records of each function of the map in `object`'s `section` (one object's map); empty when there is none.
		std::vector<std::vector<MapRecord>> MapOf(const std::string& object, llvm::StringRef section)
		{
			llvm::Expected<std::unique_ptr<llvm::object::ObjectFile>> file = llvm::object::ObjectFile::createObjectFile(llvm::MemoryBufferRef(object, "object"));
			if (!file)
			{
				llvm::consumeError(file.takeError());
				return {};
			}
			const std::optional<llvm::object::SectionRef> found = SectionNamed(**file, section);
			if (!found) return {};
			llvm::Expected<llvm::StringRef> contents = found->getContents();
			if (!contents)
			{
				llvm::consumeError(contents.takeError());
				return {};
			}
			return RecordsOf(*contents);
		}

		// The bytes of `object`'s function `name`: from its symbol to its section's end; empty when it has none.
		std::string CodeOf(const std::string& object, llvm::StringRef name)
		{
			llvm::Expected<std::unique_ptr<llvm::object::ObjectFile>> file = llvm::object::ObjectFile::createObjectFile(llvm::MemoryBufferRef(object, "object"));
			if (!file)
			{
				llvm::consumeError(file.takeError());
				return {};
			}
			for (const llvm::object::SymbolRef& symbol : (*file)->symbols())
			{
				llvm::Expected<llvm::StringRef> found = symbol.getName();
				if (!found)
				{
					llvm::consumeError(found.takeError());
					continue;
				}
				if (*found != name) continue;
				llvm::Expected<llvm::object::section_iterator> section = symbol.getSection();
				llvm::Expected<uint64_t> address = symbol.getAddress();
				if (!section || !address || *section == (*file)->section_end())
				{
					if (!section) llvm::consumeError(section.takeError());
					if (!address) llvm::consumeError(address.takeError());
					return {};
				}
				llvm::Expected<llvm::StringRef> contents = (*section)->getContents();
				if (!contents)
				{
					llvm::consumeError(contents.takeError());
					return {};
				}
				const uint64_t start = *address - (*section)->getAddress();
				return start <= contents->size() ? contents->substr(start).str() : std::string();
			}
			return {};
		}

		const char* MapSectionFor(TargetSystem system) { return system == TargetSystem::Linux ? c_StackMapSectionElf : c_StackMapSectionCoff; }

		// The length of the `call` at `at` of `code` - E8 rel32, or a REX and FF /2 with its ModRM, SIB and displacement -
		// or 0 when it is none.
		size_t CallLength(const std::string& code, size_t at)
		{
			constexpr int c_Call = 0xE8, c_Group5 = 0xFF, c_CallInGroup5 = 2, c_RexFirst = 0x40, c_RexLast = 0x4F;
			auto byte = [&](size_t i) { return at + i < code.size() ? static_cast<int>(static_cast<uint8_t>(code[at + i])) : -1; };
			if (byte(0) == c_Call) return 5;
			size_t length = byte(0) >= c_RexFirst && byte(0) <= c_RexLast ? 1 : 0;
			if (byte(length) != c_Group5) return 0;
			const int modrm = byte(length + 1);
			if (modrm < 0 || ((modrm >> 3) & 7) != c_CallInGroup5) return 0;
			const int mod = modrm >> 6, rm = modrm & 7;
			length += 2;
			if (mod == 3) return length;                                      // call reg
			const bool sib = rm == 4;
			if (sib) ++length;
			if (mod == 0) return (rm == 5 || (sib && (byte(length - 1) & 7) == 5)) ? length + 4 : length; // [rip+disp32], [disp32+index]
			return length + (mod == 1 ? 1 : 4);
		}

		// Whether the code of `code` that ends at `end` is a call.
		bool EndsACall(const std::string& code, size_t end)
		{
			constexpr size_t c_LongestCall = 8;
			if (end > code.size()) return false;
			for (size_t length = 2; length <= c_LongestCall && length <= end; ++length)
				if (CallLength(code, end - length) == length) return true;
			return false;
		}

		// The records of `object`'s stack map (one object's) that are no return address - each `function+offset` - or a
		// line saying what could not be read. A function is named by the relocation of its address in the map, so its
		// symbol must be one (a global function's on ELF).
		std::vector<std::string> RecordsAfterNoCall(const std::string& object, TargetSystem system)
		{
			llvm::Expected<std::unique_ptr<llvm::object::ObjectFile>> file = llvm::object::ObjectFile::createObjectFile(llvm::MemoryBufferRef(object, "object"));
			if (!file)
			{
				llvm::consumeError(file.takeError());
				return { "no object" };
			}
			const std::optional<llvm::object::SectionRef> map = SectionNamed(**file, MapSectionFor(system));
			if (!map) return { "no stack map" };
			std::map<uint64_t, std::string> functionAt; // by the offset of its address in the map
			auto addRelocations = [&](const llvm::object::SectionRef& holder) {
				for (const llvm::object::RelocationRef& relocation : holder.relocations())
				{
					const llvm::object::symbol_iterator symbol = relocation.getSymbol();
					if (symbol == (*file)->symbol_end()) continue;
					llvm::Expected<llvm::StringRef> name = symbol->getName();
					if (!name)
					{
						llvm::consumeError(name.takeError());
						continue;
					}
					functionAt[relocation.getOffset()] = name->str();
				}
			};
			addRelocations(*map); // COFF: a section's own
			for (const llvm::object::SectionRef& section : (*file)->sections()) // ELF: a relocation section's
			{
				llvm::Expected<llvm::object::section_iterator> relocated = section.getRelocatedSection();
				if (!relocated)
				{
					llvm::consumeError(relocated.takeError());
					continue;
				}
				if (*relocated != (*file)->section_end() && **relocated == *map) addRelocations(section);
			}
			constexpr uint64_t c_FirstAddress = 16, c_AddressBytes = 8;
			const std::vector<std::vector<MapRecord>> functions = MapOf(object, MapSectionFor(system));
			std::vector<std::string> wrong;
			for (size_t f = 0; f < functions.size(); ++f)
			{
				const auto name = functionAt.find(c_FirstAddress + f * c_AddressBytes);
				if (name == functionAt.end())
				{
					wrong.push_back("function " + std::to_string(f) + " unnamed");
					continue;
				}
				const std::string code = CodeOf(object, name->second);
				for (const MapRecord& record : functions[f])
					if (!EndsACall(code, record.offset)) wrong.push_back(name->second + "+" + std::to_string(record.offset));
			}
			return wrong;
		}

		// `instruction` as a stackmap of `id` (Compiler/StackMapFormat: -O0's slot table and records after calls); null when
		// it is none.
		const llvm::CallBase* StackMapOf(const llvm::Instruction* instruction, uint64_t id)
		{
			const auto* call = llvm::dyn_cast_or_null<llvm::IntrinsicInst>(instruction);
			if (!call || call->getIntrinsicID() != llvm::Intrinsic::experimental_stackmap) return nullptr;
			const auto* found = llvm::dyn_cast<llvm::ConstantInt>(call->getArgOperand(0));
			return found && found->getZExtValue() == id ? call : nullptr;
		}
	}

	void StackMapsTest::StackMapsAreTheDefault()
	{
		char program[] = "C4.exe", source[] = "main.cf", without[] = "-fno-stack-maps", with[] = "-fstack-maps";
		char* plain[] = { program, source };
		char* off[] = { program, source, without };
		char* offThenOn[] = { program, source, without, with };
		TEST_ASSERT(ParseArgs(2, plain).stackMaps);
		TEST_ASSERT(!ParseArgs(3, off).stackMaps);
		TEST_ASSERT(ParseArgs(4, offThenOn).stackMaps);
	}

	void StackMapsTest::WithoutTheFlagNothingChanges()
	{
		Lowered plain = Lower(kProgram, false);
		TEST_ASSERT(plain.module != nullptr);
		if (!plain.module) return;
		TEST_ASSERT(!UsesStackMaps(*plain.module));
		InsertStatepoints(*plain.module, kOptimized);
		TEST_ASSERT(Text(*plain.module) == plain.ir);
	}

	void StackMapsTest::TheFlagChangesNoFunction()
	{
		Lowered plain = Lower(kProgram, false);
		Lowered mapped = Lower(kProgram, true);
		TEST_ASSERT(plain.module != nullptr && mapped.module != nullptr);
		if (!plain.module || !mapped.module) return;
		TEST_ASSERT(UsesStackMaps(*mapped.module));
		TEST_ASSERT(mapped.module->getDataLayoutStr() == plain.module->getDataLayoutStr());
		TEST_ASSERT(mapped.ir.find("addrspace(") == std::string::npos);
		for (const llvm::Function& fn : *plain.module)
		{
			const llvm::Function* same = mapped.module->getFunction(fn.getName());
			const bool alike = same != nullptr && Text(same->getFunctionType()) == Text(fn.getFunctionType()) && same->getInstructionCount() == fn.getInstructionCount();
			if (!alike) printf("not alike: %s (%u instructions, %u with the flag)\n", fn.getName().str().c_str(), fn.getInstructionCount(), same ? same->getInstructionCount() : 0u);
			TEST_ASSERT(alike);
			if (!alike) continue;
			auto other = llvm::instructions(*same).begin();
			for (const llvm::Instruction& instruction : llvm::instructions(fn))
			{
				TEST_ASSERT(instruction.getOpcode() == other->getOpcode() && Text(instruction.getType()) == Text(other->getType()));
				++other;
			}
		}
	}

	void StackMapsTest::CallsGetRecords()
	{
		Lowered mapped = Lower(kProgram, true);
		TEST_ASSERT(mapped.module != nullptr);
		if (!mapped.module) return;
		InsertStatepoints(*mapped.module, kOptimized);
		TEST_ASSERT(Verified(*mapped.module));
		const llvm::Function* slots = FunctionNamed(*mapped.module, "Slots");
		TEST_ASSERT(slots && slots->hasGC() && slots->getGC() == c_StackMapsGc);
		if (slots) TEST_ASSERT(RecordedCalls(*slots, "Call").size() == 1);
		TEST_ASSERT(ObjectWritten(*mapped.module, HostSystem(), 0));
	}

	void StackMapsTest::ADeadSlotIsNotListed()
	{
		Lowered mapped = Lower(kProgram, true);
		TEST_ASSERT(mapped.module != nullptr);
		if (!mapped.module) return;
		InsertStatepoints(*mapped.module, kOptimized);
		const llvm::Function* slots = FunctionNamed(*mapped.module, "Slots");
		TEST_ASSERT(slots != nullptr);
		if (!slots) return;
		const std::vector<const llvm::CallBase*> calls = RecordedCalls(*slots, "Call");
		const std::vector<const llvm::CallBase*> allocations = RecordedCalls(*slots, "c4_alloc");
		TEST_ASSERT(calls.size() == 1 && allocations.size() == 3);
		if (calls.size() != 1 || allocations.size() != 3) return;
		// At the third `new` a and b are read later; at Call only c is.
		TEST_ASSERT(ListedSlots(*allocations[2]) >= 2);
		TEST_ASSERT(ListedSlots(*calls[0]) >= 1);
		TEST_ASSERT(ListedSlots(*calls[0]) < ListedSlots(*allocations[2]));
	}

	void StackMapsTest::AValueLiveAcrossACallIsListed()
	{
		llvm::LLVMContext context;
		std::unique_ptr<llvm::Module> module = Parsed(context, R"(
			@g = global ptr null
			declare ptr @make()
			declare void @call()
			declare void @use(ptr, i64)
			define void @f(ptr %a, i64 %n) {
			entry:
				%slot = alloca ptr
				%field = getelementptr i8, ptr %a, i64 8
				%made = call ptr @make()
				%dead = call ptr @make()
				%bits = ptrtoint ptr %made to i64
				%local = getelementptr i8, ptr %slot, i64 8
				call void @call()
				call void @use(ptr %field, i64 %bits)
				call void @use(ptr %a, i64 %n)
				call void @use(ptr %local, i64 0)
				call void @use(ptr @g, i64 0)
				ret void
			}
		)");
		TEST_ASSERT(module != nullptr);
		if (!module) return;
		// %field points into %a, which is listed; %made lives on only as %bits; %dead is not read again; %local is a stack
		// slot's address, @g a global's.
		const std::vector<std::string> live = LiveAcross(*module->getFunction("f"), "call");
		TEST_ASSERT(live == std::vector<std::string>({ "a", "n", "bits" }));
	}

	void StackMapsTest::ACounterIsNoAddress()
	{
		llvm::LLVMContext context;
		std::unique_ptr<llvm::Module> module = Parsed(context, R"(
			declare void @call()
			declare void @use(ptr, i64)
			define void @f(ptr %p, i32 %count, ptr %q) {
			entry:
				%start = load i64, ptr %q
				br label %body
			body:
				%i = phi i64 [ 0, %entry ], [ %next, %body ]
				%address = phi i64 [ %start, %entry ], [ %step, %body ]
				call void @call()
				%next = add i64 %i, 1
				%step = add i64 %address, 16
				%wide = zext i32 %count to i64
				%done = icmp eq i64 %next, %wide
				br i1 %done, label %exit, label %body
			exit:
				call void @use(ptr %p, i64 %step)
				ret void
			}
		)");
		TEST_ASSERT(module != nullptr);
		if (!module) return;
		// The counter starts at a constant and only adds to itself; the integer loaded and stepped may be an address.
		const std::vector<std::string> live = LiveAcross(*module->getFunction("f"), "call");
		TEST_ASSERT(live == std::vector<std::string>({ "p", "address" }));
	}

	void StackMapsTest::OnlyASiblingCallKeepsNoRecord()
	{
		llvm::LLVMContext context;
		std::unique_ptr<llvm::Module> module = Parsed(context, R"(
			declare void @collect()
			declare ptr @make()
			define i32 @returnsZero() {
				tail call void @collect()
				ret i32 0
			}
			define i64 @returnsItsAddress() {
				%made = tail call ptr @make()
				%address = ptrtoint ptr %made to i64
				ret i64 %address
			}
			define ptr @returnsAfterABranch(i1 %which) {
			entry:
				br i1 %which, label %call, label %exit
			call:
				%made = tail call ptr @make()
				br label %exit
			exit:
				%result = phi ptr [ %made, %call ], [ null, %entry ]
				ret ptr %result
			}
		)");
		TEST_ASSERT(module != nullptr);
		if (!module) return;
		InsertStatepoints(*module, kOptimized);
		TEST_ASSERT(Verified(*module));
		// A call before a return of something else is no sibling call: its frame waits in it, and is read by its record.
		TEST_ASSERT(RecordedCalls(*module->getFunction("returnsZero"), "collect").size() == 1);
		TEST_ASSERT(RecordedCalls(*module->getFunction("returnsItsAddress"), "make").empty());
		TEST_ASSERT(RecordedCalls(*module->getFunction("returnsAfterABranch"), "make").empty());
	}

	void StackMapsTest::AnInvokeGetsARecord()
	{
		constexpr const char* kGuarded = R"(
			int32 g_done = 0;
			struct Guard { public int32 v; [operator("delete")] void Done() { g_done++; } }
			void MayThrow(int32 x) { if (x > 5) throw x; }
			int32 Guarded(int32 x) { Guard g; MayThrow(x); return g.v; }
			int32 main() { return Guarded(1); }
		)";
		for (const TargetSystem system : { TargetSystem::Windows, TargetSystem::Linux })
		{
			Lowered mapped = Lower(kGuarded, true, system);
			TEST_ASSERT(mapped.module != nullptr);
			if (!mapped.module) continue;
			InsertStatepoints(*mapped.module, kOptimized);
			TEST_ASSERT(Verified(*mapped.module));
			const llvm::Function* guarded = FunctionNamed(*mapped.module, "Guarded");
			TEST_ASSERT(guarded != nullptr);
			if (!guarded) continue;
			size_t invokes = 0;
			for (const llvm::Instruction& instruction : llvm::instructions(*guarded))
				if (const auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(&instruction))
				{
					++invokes;
					TEST_ASSERT(invoke->getOperandBundle(llvm::LLVMContext::OB_deopt).has_value());
				}
			TEST_ASSERT(invokes >= 1);
			const std::optional<std::string> object = ObjectOf(*mapped.module, system, 0);
			TEST_ASSERT(object.has_value());
			// On Windows code generation puts a nop after a call whose exception state changes after it: the record stays
			// at the return address (StackMapPlacement). Its functions are internal: ELF names them by their section.
			if (object && system == TargetSystem::Windows)
				for (const std::string& wrong : RecordsAfterNoCall(*object, system))
				{
					printf("no return address: %s\n", wrong.c_str());
					TEST_ASSERT(false);
				}
			// At -O0 an invoke is recorded by a stackmap at the start of its normal destination, which code generation moves
			// back to right after its call.
			Lowered atO0 = Lower(kGuarded, true, system);
			TEST_ASSERT(atO0.module != nullptr);
			if (!atO0.module) continue;
			InsertStatepoints(*atO0.module, llvm::CodeGenOptLevel::None);
			TEST_ASSERT(Verified(*atO0.module));
			llvm::Function* guardedAtO0 = nullptr;
			for (llvm::Function& fn : *atO0.module)
				if (!fn.isDeclaration() && fn.getName().starts_with("Guarded")) guardedAtO0 = &fn;
			TEST_ASSERT(guardedAtO0 != nullptr);
			if (!guardedAtO0) continue;
			for (llvm::Instruction& instruction : llvm::instructions(*guardedAtO0))
				if (auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(&instruction))
				{
					const llvm::Function* callee = invoke->getCalledFunction();
					if (!callee || !callee->getName().starts_with("MayThrow")) continue;
					TEST_ASSERT(!invoke->getOperandBundle(llvm::LLVMContext::OB_deopt));
					TEST_ASSERT(StackMapOf(RecordPlace(*invoke), c_CallRecordId) != nullptr);
				}
			const std::optional<std::string> objectAtO0 = ObjectOf(*atO0.module, system, 0);
			TEST_ASSERT(objectAtO0.has_value());
			if (!objectAtO0) continue;
			size_t records = 0;
			for (const std::vector<MapRecord>& function : MapOf(*objectAtO0, MapSectionFor(system)))
				for (const MapRecord& record : function)
				{
					++records;
					TEST_ASSERT(!record.whole);
				}
			TEST_ASSERT(records >= 1);
			if (system == TargetSystem::Windows)
				for (const std::string& wrong : RecordsAfterNoCall(*objectAtO0, system))
				{
					printf("no return address at -O0: %s\n", wrong.c_str());
					TEST_ASSERT(false);
				}
		}
	}

	void StackMapsTest::ManyValuesAtOneCallFitTheFrame()
	{
		// Twelve fields of one object and twelve pointers loaded from them, all read after the call: code generation could
		// make each field's address again at the call from the object's, which wants a register apiece there.
		std::string body = "declare void @call()\ndeclare void @use(ptr)\ndefine void @f(ptr %a) {\n";
		for (int i = 0; i < 12; ++i)
			body += "%f" + std::to_string(i) + " = getelementptr i8, ptr %a, i64 " + std::to_string(8 * i + 16) + "\n%p" + std::to_string(i) +
				" = load ptr, ptr %f" + std::to_string(i) + "\n";
		body += "call void @call()\n";
		for (int i = 0; i < 12; ++i) body += "call void @use(ptr %f" + std::to_string(i) + ")\ncall void @use(ptr %p" + std::to_string(i) + ")\n";
		body += "ret void\n}\n";
		for (const TargetSystem system : { TargetSystem::Windows, TargetSystem::Linux })
		{
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> module = Parsed(context, body, system);
			TEST_ASSERT(module != nullptr);
			if (module) TEST_ASSERT(ObjectWritten(*module, system, 2));
		}
	}

	void StackMapsTest::AtO0ACallIsRecordedAfterIt()
	{
		// Twenty-four pointers and a pair of them live across one call. -O0's register allocator cannot spill a statepoint's
		// register operands: with each value in place it ran out of registers. So at -O0 each goes through a slot of its own,
		// and the call is recorded by a stackmap after it, masks over the function's slot table: fast instruction selection
		// lowers that itself, where it hands a statepoint to SelectionDAG.
		constexpr int kPointers = 24;
		std::string body = "declare void @call()\ndeclare void @use(ptr)\ndeclare { ptr, ptr } @pair()\ndefine void @f(ptr %a) {\n%two = call { ptr, ptr } @pair()\n";
		for (int i = 0; i < kPointers; ++i)
			body += "%f" + std::to_string(i) + " = getelementptr i8, ptr %a, i64 " + std::to_string(8 * i) + "\n%p" + std::to_string(i) + " = load ptr, ptr %f" +
				std::to_string(i) + "\n";
		body += "call void @call()\n";
		for (int i = 0; i < kPointers; ++i) body += "call void @use(ptr %p" + std::to_string(i) + ")\n";
		body += "%first = extractvalue { ptr, ptr } %two, 0\ncall void @use(ptr %first)\n%second = extractvalue { ptr, ptr } %two, 1\n"
				"call void @use(ptr %second)\nret void\n}\n";
		for (const TargetSystem system : { TargetSystem::Windows, TargetSystem::Linux })
		{
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> module = Parsed(context, body, system);
			TEST_ASSERT(module != nullptr);
			if (!module) continue;
			InsertStatepoints(*module, llvm::CodeGenOptLevel::None);
			TEST_ASSERT(Verified(*module));
			const llvm::Function& f = *module->getFunction("f");
			TEST_ASSERT(RecordedCalls(f, "call").empty()); // no statepoint
			const llvm::CallBase* call = nullptr;
			const llvm::CallBase* table = nullptr;
			for (const llvm::Instruction& instruction : llvm::instructions(f))
			{
				if (const auto* each = llvm::dyn_cast<llvm::CallBase>(&instruction); each && each->getCalledFunction() && each->getCalledFunction()->getName() == "call")
					call = each;
				if (const llvm::CallBase* found = StackMapOf(&instruction, c_SlotTableId)) table = found;
			}
			TEST_ASSERT(call != nullptr && table != nullptr);
			if (!call || !table) continue;
			TEST_ASSERT(table->getParent() == &f.getEntryBlock());
			const llvm::CallBase* record = StackMapOf(call->getNextNode(), c_CallRecordId);
			TEST_ASSERT(record != nullptr);
			if (!record) continue;
			size_t tableSlots = 0;
			for (const llvm::Use& operand : table->args()) tableSlots += llvm::isa<llvm::AllocaInst>(operand.get()) ? 1 : 0;
			size_t listed = 0;
			for (unsigned i = 2; i < record->arg_size(); ++i)
			{
				const auto* mask = llvm::dyn_cast<llvm::ConstantInt>(record->getArgOperand(i));
				TEST_ASSERT(mask != nullptr);
				if (mask) listed += static_cast<size_t>(std::popcount(static_cast<uint32_t>(mask->getZExtValue())));
			}
			TEST_ASSERT(record->arg_size() - 2 == (tableSlots + 31) / 32);
			TEST_ASSERT(listed == kPointers + 1); // each pointer's slot and the pair's
			// Written, the record lists them, and InsertStatepoints run again records nothing twice.
			const std::optional<std::string> object = ObjectOf(*module, system, 0);
			TEST_ASSERT(object.has_value());
			if (!object) continue;
			const std::vector<std::vector<MapRecord>> map = MapOf(*object, MapSectionFor(system));
			TEST_ASSERT(map.size() == 1);
			if (map.size() != 1) continue;
			const auto atCall = std::find_if(map[0].begin(), map[0].end(), [](const MapRecord& r) { return r.slots == kPointers + 1; });
			TEST_ASSERT(atCall != map[0].end() && !atCall->whole && atCall->values == 0);
			TEST_ASSERT(map[0].size() == kPointers + 4); // pair, call, then the uses: each call once
		}
	}

	void StackMapsTest::ARecordIsAtItsCallsReturnAddress()
	{
		// The collector finds a frame's record by the return address it waits at. At -O0 the register allocator puts the
		// call's result copies and reloads between a call and its record (StackMapPlacement moves the record back to the
		// call); optimized, the record is the statepoint's. Every call here is a direct one: `call rel32`, 0xE8.
		constexpr const char* kBody = R"(
			declare ptr @make()
			declare void @use(ptr, ptr)
			define void @f(ptr %a) {
			entry:
				%p = call ptr @make()
				%q = call ptr @make()
				call void @use(ptr %p, ptr %a)
				%r = call ptr @make()
				call void @use(ptr %q, ptr %r)
				call void @use(ptr %a, ptr %p)
				ret void
			}
			define void @realigned(ptr %a) {
			entry:
				%wide = alloca <8 x float>, align 32
				%slot = alloca ptr
				store ptr %a, ptr %slot
				%p = call ptr @make()
				call void @use(ptr %p, ptr %slot)
				call void @use(ptr %wide, ptr %p)
				ret void
			}
		)";
		// f's six calls; `realigned`'s three, in a frame that realigns the stack (its slots are named from rsp).
		constexpr size_t kCalls = 6;
		constexpr size_t kRealignedCalls = 3;
		for (const TargetSystem system : { TargetSystem::Windows, TargetSystem::Linux })
			for (const int optLevel : { 0, 2 })
			{
				llvm::LLVMContext context;
				std::unique_ptr<llvm::Module> module = Parsed(context, kBody, system);
				TEST_ASSERT(module != nullptr);
				if (!module) continue;
				const std::optional<std::string> object = ObjectOf(*module, system, optLevel);
				TEST_ASSERT(object.has_value());
				if (!object) continue;
				const std::vector<std::vector<MapRecord>> map = MapOf(*object, MapSectionFor(system));
				TEST_ASSERT(map.size() == 2 && map[0].size() == kCalls && map[1].size() == kRealignedCalls);
				for (const std::vector<MapRecord>& function : map)
					for (const MapRecord& record : function) TEST_ASSERT(!record.whole);
				// The last call is followed by the epilogue, which on Windows wants a nop after the call.
				for (const std::string& wrong : RecordsAfterNoCall(*object, system))
				{
					printf("no return address: %s (-O%d, %s)\n", wrong.c_str(), optLevel, MapSectionFor(system));
					TEST_ASSERT(false);
				}
			}
	}

	void StackMapsTest::ACallThatCannotCollectHasNoRecord()
	{
		// A call that cannot start a collection needs no record: of a C library function, or of a function of the module
		// whose calls cannot (also through recursion); a declaration of another kind may (C4's runtime allocates), and so may
		// a function that calls one, a call through a pointer, and what calls those.
		llvm::LLVMContext context;
		std::unique_ptr<llvm::Module> module = Parsed(context, R"(
			declare ptr @alloc()
			declare i64 @strlen(ptr)
			declare void @use(ptr, i64)
			define i64 @pure(i64 %x) {
				%y = add i64 %x, 1
				ret i64 %y
			}
			define i64 @measures(ptr %p) {
				%n = call i64 @strlen(ptr %p)
				%m = call i64 @pure(i64 %n)
				ret i64 %m
			}
			define i64 @counts(i64 %n) {
			entry:
				%done = icmp eq i64 %n, 0
				br i1 %done, label %exit, label %more
			more:
				%less = sub i64 %n, 1
				%r = call i64 @counts(i64 %less)
				br label %exit
			exit:
				%v = phi i64 [ 0, %entry ], [ %r, %more ]
				ret i64 %v
			}
			define ptr @allocates() {
				%p = call ptr @alloc()
				ret ptr %p
			}
			define ptr @allocatesThrough() {
				%p = call ptr @allocates()
				ret ptr %p
			}
			define i64 @calls(ptr %fn) {
				%r = call i64 %fn(i64 1)
				ret i64 %r
			}
			define void @f(ptr %a, ptr %fn) {
				%x = call i64 @pure(i64 1)
				%n = call i64 @measures(ptr %a)
				%c = call i64 @counts(i64 %n)
				%p = call ptr @allocatesThrough()
				%r = call i64 @calls(ptr %fn)
				call void @use(ptr %p, i64 %x)
				call void @use(ptr %a, i64 %c)
				call void @use(ptr %a, i64 %r)
				ret void
			}
		)");
		TEST_ASSERT(module != nullptr);
		if (!module) return;
		InsertStatepoints(*module, kOptimized);
		TEST_ASSERT(Verified(*module));
		const llvm::Function& f = *module->getFunction("f");
		for (const char* never : { "pure", "measures", "counts" }) TEST_ASSERT(RecordedCalls(f, never).empty());
		for (const char* may : { "allocatesThrough", "calls", "use" }) TEST_ASSERT(!RecordedCalls(f, may).empty());
		TEST_ASSERT(RecordedCalls(*module->getFunction("measures"), "strlen").empty());
		TEST_ASSERT(RecordedCalls(*module->getFunction("counts"), "counts").empty());
		TEST_ASSERT(RecordedCalls(*module->getFunction("allocatesThrough"), "allocates").size() == 1);
		// Only a function with records gets the strategy.
		TEST_ASSERT(f.hasGC() && !module->getFunction("pure")->hasGC() && !module->getFunction("counts")->hasGC());
	}

	void StackMapsTest::RecordsAreTheSameEveryTime()
	{
		// Each `make` has the values made before it live across it, and at -O0 each value goes through a slot of its own:
		// made in the calls' order, whatever the addresses of the module's values. The modules stay alive, so each round's
		// values are at other addresses.
		constexpr int kValues = 12;
		constexpr int kRounds = 8;
		std::string body = "declare ptr @make()\ndeclare void @use(ptr)\ndefine void @f() {\n";
		for (int i = 0; i < kValues; ++i) body += "%v" + std::to_string(i) + " = call ptr @make()\n";
		for (int i = 0; i < kValues; ++i) body += "call void @use(ptr %v" + std::to_string(i) + ")\n";
		body += "ret void\n}\n";
		std::vector<std::unique_ptr<llvm::LLVMContext>> contexts;
		std::vector<std::unique_ptr<llvm::Module>> modules;
		std::vector<std::string> texts;
		for (int round = 0; round < kRounds; ++round)
		{
			contexts.push_back(std::make_unique<llvm::LLVMContext>());
			modules.push_back(Parsed(*contexts.back(), body));
			TEST_ASSERT(modules.back() != nullptr);
			if (!modules.back()) return;
			InsertStatepoints(*modules.back(), llvm::CodeGenOptLevel::None);
			texts.push_back(Text(*modules.back()));
		}
		for (const std::string& text : texts) TEST_ASSERT(text == texts.front());
	}

	void StackMapsTest::AnElfStackMapIsWritableAndBracketed()
	{
		Lowered mapped = Lower(kProgram, true, TargetSystem::Linux);
		TEST_ASSERT(mapped.module != nullptr);
		if (!mapped.module) return;
		const std::optional<std::string> object = ObjectOf(*mapped.module, TargetSystem::Linux, 0);
		TEST_ASSERT(object.has_value());
		if (!object) return;
		llvm::Expected<std::unique_ptr<llvm::object::ObjectFile>> file = llvm::object::ObjectFile::createObjectFile(llvm::MemoryBufferRef(*object, "object"));
		TEST_ASSERT(static_cast<bool>(file));
		if (!file)
		{
			llvm::consumeError(file.takeError());
			return;
		}
		// The linker brackets a section named as a C identifier with __start_/__stop_ symbols.
		const std::optional<llvm::object::SectionRef> section = SectionNamed(**file, c_StackMapSectionElf);
		TEST_ASSERT(section.has_value());
		if (!section) return;
		const uint64_t flags = llvm::object::ELFSectionRef(*section).getFlags();
		TEST_ASSERT((flags & llvm::ELF::SHF_WRITE) != 0);
		TEST_ASSERT((flags & llvm::ELF::SHF_GNU_RETAIN) != 0);
		TEST_ASSERT(!MapOf(*object, c_StackMapSectionElf).empty());
		TEST_ASSERT(!SectionNamed(**file, ".llvm_stackmaps").has_value()); // LLVM's own format is not written
	}

	void StackMapsTest::TheMapNamesEachCallsValuesAndSlots()
	{
		// At `call`: `a`, `n` and `bits` are read after it, and `slot`, whose address goes to `use`, is listed at every call.
		constexpr const char* kBody = R"(
			declare ptr @make()
			declare void @call()
			declare void @use(ptr, i64)
			define void @f(ptr %a, i64 %n) {
			entry:
				%slot = alloca ptr
				%made = call ptr @make()
				%bits = ptrtoint ptr %made to i64
				call void @call()
				call void @use(ptr %a, i64 %n)
				call void @use(ptr %slot, i64 %bits)
				ret void
			}
		)";
		for (const TargetSystem system : { TargetSystem::Windows, TargetSystem::Linux })
			for (const int optLevel : { 2, 0 })
			{
				llvm::LLVMContext context;
				std::unique_ptr<llvm::Module> module = Parsed(context, kBody, system);
				TEST_ASSERT(module != nullptr);
				if (!module) continue;
				const std::optional<std::string> object = ObjectOf(*module, system, optLevel);
				TEST_ASSERT(object.has_value());
				if (!object) continue;
				const std::vector<std::vector<MapRecord>> map = MapOf(*object, system == TargetSystem::Linux ? c_StackMapSectionElf : c_StackMapSectionCoff);
				TEST_ASSERT(map.size() == 1 && map[0].size() == 4); // f's calls: make, call, use, use
				if (map.size() != 1 || map[0].size() != 4) continue;
				const MapRecord& atCall = map[0][1];
				TEST_ASSERT(!atCall.whole);
				// Optimized, the three values are where they are; at -O0 each through a slot of its own.
				TEST_ASSERT(atCall.values == (optLevel == 0 ? 0u : 3u));
				TEST_ASSERT(atCall.slots == (optLevel == 0 ? 4u : 1u));
				TEST_ASSERT(map[0][3].values == 0 && map[0][3].slots == 1); // after the last call nothing is read but `slot`
			}
	}

	void StackMapsTest::ALibraryCallCodeGenerationExpandsHasNoRecord()
	{
		// Optimized, a memcmp compared with 0 becomes bcmp where the C library has one (Linux), and code generation expands
		// it in place; a record would hide the call from it.
		Lowered mapped = Lower(R"(
			struct T { public int64 v; }
			uint64 g_size = 8;
			int32 Same(T* a, T* b) {
				if (memcmp((void*)a, (void*)b, g_size) == 0) return 1;
				return 0;
			}
			int32 main() { T* a = new T; T* b = new T; return Same(a, b); }
		)", true, TargetSystem::Linux);
		TEST_ASSERT(mapped.module != nullptr);
		if (!mapped.module) return;
		const CpuTarget target = CpuTarget().On(TargetSystem::Linux);
		const ObjectSettings settings{ 2, &target, 1, false };
		std::vector<ObjectDiagnostic> diagnostics;
		TEST_ASSERT(OptimizeModule(*mapped.module, settings, diagnostics));
		InsertStatepoints(*mapped.module, kOptimized);
		TEST_ASSERT(Verified(*mapped.module));
		size_t compares = 0;
		for (const llvm::Function& fn : *mapped.module)
			for (const llvm::Instruction& instruction : llvm::instructions(fn))
				if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction))
					if (const llvm::Function* callee = call->getCalledFunction(); callee && (callee->getName() == "bcmp" || callee->getName() == "memcmp"))
					{
						++compares;
						TEST_ASSERT(!call->getOperandBundle(llvm::LLVMContext::OB_deopt));
					}
		TEST_ASSERT(compares >= 1);
		TEST_ASSERT(ObjectWritten(*mapped.module, TargetSystem::Linux, 2));
	}

	void StackMapsTest::OnlyAProgramWithStackMapsLinksTheirRuntime()
	{
		TEST_ASSERT(RuntimeSourceIsForStackMaps("GC/GCStackMaps.c"));
		TEST_ASSERT(RuntimeSourceIsForStackMaps("GC/GCStackWalkWin32.c"));
		TEST_ASSERT(RuntimeSourceIsForStackMaps("GC/GCStackWalkPosix.c"));
		TEST_ASSERT(!RuntimeSourceIsForStackMaps("GC/GCPlatformWin32.c"));
		TEST_ASSERT(!RuntimeSourceIsForStackMaps("GC/GCThreads.c"));
		const TargetAbi& windows = TargetAbi::For(TargetSystem::Windows);
		auto defines = [](const std::vector<std::string>& command) {
			return std::find(command.begin(), command.end(), std::string("-DC4_STACK_MAPS=1")) != command.end();
		};
		TEST_ASSERT(defines(RuntimeCompileCommand("clang", "runtime", "runtime/GC/GCThreads.c", "GCThreads.obj", windows, "", true)));
		TEST_ASSERT(!defines(RuntimeCompileCommand("clang", "runtime", "runtime/GC/GCThreads.c", "GCThreads.obj", windows, "")));
	}
} // namespace Testing
