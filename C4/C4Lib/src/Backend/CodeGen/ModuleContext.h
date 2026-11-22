#pragma once
#include <map>
#include "Model/Types.h"
#include "Model/SymbolTable.h"
#include "Backend/CodeGen/Abi.h"
#include "Backend/CodeGen/AliasTags.h"
#include "Target/TargetAbi.h"
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Scripting
{
	class ProgramModel; // GlobalAddress resolves function-pointer initializers against it
	class RttiEmitter;
	class VtableEmitter;
	class EhTables;
	class CensusTypes;

	/// Program-wide LLVM state (doc 03). Owns the single llvm::Module every function, global, and vtable is
	/// emitted into, the C4-type -> LLVM-type cache, the Abi brick, and the function-signature cache. Created
	/// once per compile; every per-function IRContext borrows it. The module owns every LLVM value ever
	/// emitted - nothing here deletes IR values.
	class ModuleContext
	{
	public:
		/// A module of a program for `system` (D-86): its target (R11) gives its triple, data layout and calling convention.
		ModuleContext(llvm::LLVMContext& ctx, TargetSystem system);
		~ModuleContext();

		TargetSystem System() const { return m_Target.System(); }
		/// What the target decides of the code emitted: emitters ask it instead of branching on the system.
		const TargetAbi& Target() const { return m_Target; }
		llvm::Module& module() { return *m_Module; }
		/// The module, which the caller then owns; nothing may use this context after it (BUG-240: it owns it otherwise).
		std::unique_ptr<llvm::Module> TakeModule() { return std::move(m_OwnedModule); }
		llvm::LLVMContext& context() { return m_Context; }
		const llvm::DataLayout& layout() const { return m_DataLayout; }
		Abi& abi() { return m_Abi; }

		/// `-fstack-maps` (R16 phase 3): a pointer the collector must find - a C4 pointer or reference, `this`, the sret
		/// pointer, a func's environment - is a managed pointer, in address space 1, whose values live across calls the
		/// stack maps record (Compiler/StackMaps); a stack slot's, a global's or code's address is a raw one, in 0, and
		/// becomes a managed one by an addrspacecast where a pointer value is made of it. Without it both are 0 and the IR
		/// is what it always was. Before anything is lowered.
		void UseStackMaps();
		bool StackMaps() const { return m_ManagedSpace != 0; }
		llvm::PointerType* ManagedPtr() const { return llvm::PointerType::get(m_Context, m_ManagedSpace); }
		llvm::PointerType* RawPtr() const { return llvm::PointerType::get(m_Context, 0); }
		/// `value` as `type` where both are pointers of different address spaces (an addrspacecast); else `value` as it is.
		static llvm::Value* InSpace(llvm::IRBuilder<>& B, llvm::Value* value, llvm::Type* type);
		static llvm::Constant* InSpace(llvm::Constant* value, llvm::Type* type);
		/// `args` as a call of `type` takes them: each pointer in its parameter's address space.
		static std::vector<llvm::Value*> Arguments(llvm::IRBuilder<>& B, llvm::FunctionType* type, llvm::ArrayRef<llvm::Value*> args);

		// The single C4 -> LLVM type map, cached by const Type*. Primitives -> exact-width int/float;
		// pointers/references/string/funcptr -> opaque ptr; arrays -> [N x elem]; objects/unions -> a named
		// StructType (opaque until FillStruct); enums -> their integer base; swizzle -> the SIMD vector.
		llvm::Type* Lower(const Type* t);
		/// A number, bool or char constant as the storage `ty` of a `type`.
		llvm::Constant* NumberConstant(const ConstantValue& value, const Type* type, llvm::Type* ty);
		/// The type `t` takes as a member of a struct or an array: Lower(t), except a vector LLVM would make
		/// larger than the model (a <3 x float> is 16 bytes to LLVM, 12 to C4) is its lanes as an array.
		llvm::Type* Storage(const Type* t);

		// Create-or-fetch the named struct type for an object/union, OPAQUE at first so recursive and
		// incomplete types are legal. GlobalEmitter fills it from the model's field offsets (FillStruct).
		llvm::StructType* StructFor(const ObjectType* obj, const char* name);

		// Fill a previously-opaque struct type: the own vptr, each base sub-object and each field at its model
		// offset (statics excluded, anonymous-union regions as one slot). The model is the layout authority -
		// this only transcribes offsets and types, never re-packs.
		void FillStruct(const ObjectType* obj);

		// Build (or fetch) the llvm::Function for a record: `this` pointer first for methods, an sret hidden
		// out-ptr prepended when the return is a too-large aggregate, params per the Abi brick. Centralizing
		// signature building here guarantees the forward-decl pass and the body pass emit identical types.
		llvm::Function* GetOrCreateFunction(const FunctionRecord& rec);
		/// `rec`'s function when this module has it, else null.
		llvm::Function* FunctionIfDeclared(const FunctionRecord& rec) const
		{
			const auto found = m_FuncCache.find(&rec);
			return found == m_FuncCache.end() ? nullptr : found->second;
		}
		/// The LLVM type of `rec`'s function: its hidden parameters, then the declared ones as the ABI passes them.
		llvm::FunctionType* FunctionTypeOf(const FunctionRecord& rec);
		/// Where this module keeps the thunk of a func signature (FuncThunk): one per signature type, null until made.
		llvm::Function*& FuncThunkSlot(const Type* signature) { return m_FuncThunks[signature]; }
		/// The records whose functions were created since the last call: what emitted code referenced for the first time,
		/// which lowering must have lowered (ProgramEmitter checks: lowering, not codegen, decides what lowers).
		std::vector<const FunctionRecord*> TakeNewFunctions() { return std::move(m_NewFunctions); }

		// The address (opaque ptr) of a model global's backing storage, created on first use. Used by lvalue
		// resolution for LValue::Global and by EmitGcRoots for the __c4_gc_roots table.
		llvm::GlobalVariable* GlobalAddress(const SymbolInfo& sym);

		/// In a script (docs/runtime/04 section 2), a thread_local that `startsOnFirstUse` picks starts on its first use
		/// on each thread: code that takes its address calls its start first.
		void StartThreadLocalsOnFirstUse(std::function<bool(const SymbolInfo&)> startsOnFirstUse) { m_StartsOnFirstUse = std::move(startsOnFirstUse); }
		/// The function that starts `sym` on the calling thread (`__c4_tls_start_<symbol>`), declared when first asked
		/// for; null when `sym` does not start on its first use.
		llvm::Function* FirstUseStart(const SymbolInfo& sym);
		/// Every start FirstUseStart declared, by its variable.
		const std::unordered_map<const SymbolInfo*, llvm::Function*>& FirstUseStarts() const { return m_FirstUseStarts; }

		// A static local's storage, created while its function is emitted; EmitGcRoots lists those that may hold
		// managed pointers.
		struct StaticStorage { llvm::GlobalVariable* storage = nullptr; const Type* type = nullptr; };
		void NoteStaticStorage(llvm::GlobalVariable* storage, const Type* type) { m_StaticStorage.push_back({ storage, type }); }
		const std::vector<StaticStorage>& StaticStorages() const { return m_StaticStorage; }

		// A C4Runtime entry point (c4_alloc / c4_free / ...): create-or-fetch an external
		// declaration with exactly this signature. The runtime is always linked in, so these never need a body.
		llvm::Function* RuntimeFunction(const char* name, llvm::Type* ret, std::vector<llvm::Type*> params);
		/// The LLVM constant of a folded value of `type` (a string or GUID is the address of its bytes).
		llvm::Constant* ConstantOf(const ConstantValue& value, const Type* type);
		/// The private global holding a string constant's code units and its NUL terminator, one per
		/// distinct (width, units) pair.
		llvm::GlobalVariable* StringGlobal(const ConstantValue& value);
		/// Zeroes the `type` at `at`: one memset for an object or array (a zero aggregate store is split into a
		/// store per element), a plain store otherwise.
		void StoreZero(llvm::IRBuilder<>& B, const Type* type, llvm::Value* at);

		// The extern _CxxThrowException(ptr value, ptr throwinfo): noreturn; performs the OS-level SEH raise.
		llvm::Function* GetCxxThrowException();
		// The personality routine every catching function installs (TargetAbi::Personality): the extern
		// __CxxFrameHandler3(...) on Windows, the C4 runtime's __c4_personality_v0(...) on Linux (D-86).
		llvm::Function* Personality();

		// Where `rec`'s hidden parameters (this, sret) sit in its LLVM signature (in the target's order).
		Abi::Signature SignatureOf(const FunctionRecord& rec) const;

		// The RTTI, vtables and EH tables of the program, MSVC-shaped on every target (docs/compiler/04).
		RttiEmitter& rtti() { return *m_Rtti; }
		VtableEmitter& vtables() { return *m_Vtables; }
		EhTables& eh() { return *m_Eh; }
		/// The census's record of each type `new` makes.
		CensusTypes& census() { return *m_Census; }

		// The model emission reads layouts and symbols from: read only, so partitions emit at once (D-81). Set once
		// before any emission.
		void SetModel(const ProgramModel* model) { m_Model = model; }
		const ProgramModel* model() const { return m_Model; }
		/// The function each function-pointer global starts at (`funcptr<R(a)> g = F;` starts at @F, not null), as
		/// lowering resolved it (LoweredProgram).
		void UseFunctionPointerStarts(const std::unordered_map<const SymbolInfo*, const FunctionRecord*>* starts) { m_FunctionPointerStarts = starts; }

		/// `-g` (B28): the module carries debug information; null when it does not.
		void EnableDebugInfo(std::unordered_map<uint32, std::string> sourcePaths);
		class DebugInfo* Debug() const { return m_Debug.get(); }

		/// The alias tags of the program's loads and stores (D-64); `-fno-strict-aliasing` disables them.
		AliasTags& aliasTags() { return m_AliasTags; }

		/// This module is one partition of a split program (D-81): a symbol that would be local, which another partition
		/// may name, is a hidden global named with kLocalSuffix, and a global's storage is defined only where
		/// `definesGlobal` says (declared elsewhere).
		void SetPartition(std::function<bool(const SymbolInfo&)> definesGlobal);
		bool InSplitProgram() const { return m_Split; }
		/// `value` (just created) as a local of the program: internal, or in a split program a hidden global another
		/// partition reaches by its name.
		void MakeLocal(llvm::GlobalValue& value) const;
		/// The name a local of the program takes in this module.
		std::string LocalName(const std::string& name) const;
		/// A declaration of a static local another partition defines, listed with this module's own (StaticStorages).
		void DeclareStaticStorage(const std::string& name, const Type* type, bool threadLocal);
		/// While a function another partition defines is copied here (FunctionEmitter::EmitImport), its static storage
		/// and once guards are that partition's: Storage declares them.
		void SetImporting(bool importing) { m_Importing = importing; }
		bool Importing() const { return m_Importing; }
		/// A local of the program named `name` that another partition defines, declared once (`type`, as it is stored).
		llvm::GlobalVariable* DeclaredLocal(const std::string& name, llvm::Type* type, bool threadLocal);
		/// Every function this module has, by the record it is for.
		std::unordered_map<const llvm::Function*, const FunctionRecord*> RecordsOfFunctions() const;

	private:
		struct StructPiece { size_t offset = 0; llvm::Type* type = nullptr; size_t size = 0; };
		std::string GlobalName(const SymbolInfo& sym) const;
		llvm::GlobalVariable* DeclareGlobal(const SymbolInfo& sym);
		std::vector<StructPiece> StructPieces(const ObjectType* obj);
		void PassOnStack(llvm::Function& fn, const FunctionRecord& rec);

		const ProgramModel* m_Model = nullptr;
		const std::unordered_map<const SymbolInfo*, const FunctionRecord*>* m_FunctionPointerStarts = nullptr;
		llvm::LLVMContext& m_Context;
		const TargetAbi& m_Target;
		std::unique_ptr<llvm::Module> m_OwnedModule;
		llvm::Module* m_Module = nullptr;
		llvm::DataLayout m_DataLayout;
		Abi m_Abi;
		std::unordered_map<const Type*, llvm::Type*> m_TypeCache;
		std::unordered_map<const FunctionRecord*, llvm::Function*> m_FuncCache;
		std::unordered_map<const Type*, llvm::Function*> m_FuncThunks;
		std::vector<const FunctionRecord*> m_NewFunctions; // created since the last TakeNewFunctions
		std::unordered_map<const SymbolInfo*, llvm::GlobalVariable*> m_GlobalCache;
		std::function<bool(const SymbolInfo&)> m_StartsOnFirstUse;
		std::unordered_map<const SymbolInfo*, llvm::Function*> m_FirstUseStarts;
		std::unordered_set<const SymbolInfo*> m_StartsWithThread; // asked of FirstUseStart: not one
		std::map<std::pair<uint8, std::string>, llvm::GlobalVariable*> m_StringGlobals;
		std::vector<StaticStorage> m_StaticStorage;
		std::unique_ptr<RttiEmitter> m_Rtti;
		std::unique_ptr<VtableEmitter> m_Vtables;
		std::unique_ptr<EhTables> m_Eh;
		std::unique_ptr<CensusTypes> m_Census;
		std::unique_ptr<class DebugInfo> m_Debug;
		AliasTags m_AliasTags;
		unsigned m_ManagedSpace = 0; // 1 with stack maps
		bool m_Split = false;
		bool m_Importing = false;
		std::function<bool(const SymbolInfo&)> m_DefinesGlobal;
	};
} // namespace Scripting
