#pragma once
#include <map>
#include "Model/Types.h"
#include "Model/SymbolTable.h"
#include "Backend/CodeGen/Abi.h"
#include "Backend/CodeGen/AliasTags.h"
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

	/// Program-wide LLVM state (doc 03). Owns the single llvm::Module every function, global, and vtable is
	/// emitted into, the C4-type -> LLVM-type cache, the Abi brick, and the function-signature cache. Created
	/// once per compile; every per-function IRContext borrows it. The module owns every LLVM value ever
	/// emitted - nothing here deletes IR values.
	class ModuleContext
	{
	public:
		explicit ModuleContext(llvm::LLVMContext& ctx);
		~ModuleContext();

		llvm::Module& module() { return *m_Module; }
		/// The module, which the caller then owns; nothing may use this context after it (BUG-240: it owns it otherwise).
		std::unique_ptr<llvm::Module> TakeModule() { return std::move(m_OwnedModule); }
		llvm::LLVMContext& context() { return m_Context; }
		const llvm::DataLayout& layout() const { return m_DataLayout; }
		Abi& abi() { return m_Abi; }

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
		/// The records whose functions were created since the last call: what emitted code referenced for
		/// the first time (the driver lowers the template-made ones among them, D-16).
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
		// The extern __CxxFrameHandler3(...): the personality routine every catching function installs.
		llvm::Function* GetFrameHandler3();

		// Where `rec`'s hidden parameters (this, sret) sit in its LLVM signature (MSVC x64 order).
		Abi::Signature SignatureOf(const FunctionRecord& rec) const;

		// The MSVC RTTI, vtables and EH tables of the program (docs/compiler/04).
		RttiEmitter& rtti() { return *m_Rtti; }
		VtableEmitter& vtables() { return *m_Vtables; }
		EhTables& eh() { return *m_Eh; }

		// The model GlobalAddress uses to resolve function-pointer initializers (`funcptr<R(a)> g = F;`
		// must start at @F, not null). Set once before any lowering.
		void SetModel(const ProgramModel* model) { m_Model = model; }
		const ProgramModel* model() const { return m_Model; }

		/// `-g` (B28): the module carries debug information; null when it does not.
		void EnableDebugInfo(std::unordered_map<uint32, std::string> sourcePaths);
		class DebugInfo* Debug() const { return m_Debug.get(); }

		/// The alias tags of the program's loads and stores (D-64); `-fno-strict-aliasing` disables them.
		AliasTags& aliasTags() { return m_AliasTags; }

	private:
		struct StructPiece { size_t offset = 0; llvm::Type* type = nullptr; size_t size = 0; };
		std::vector<StructPiece> StructPieces(const ObjectType* obj);

		const ProgramModel* m_Model = nullptr;
		llvm::LLVMContext& m_Context;
		std::unique_ptr<llvm::Module> m_OwnedModule;
		llvm::Module* m_Module = nullptr;
		llvm::DataLayout m_DataLayout;
		Abi m_Abi;
		std::unordered_map<const Type*, llvm::Type*> m_TypeCache;
		std::unordered_map<const FunctionRecord*, llvm::Function*> m_FuncCache;
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
		std::unique_ptr<class DebugInfo> m_Debug;
		AliasTags m_AliasTags;
	};
} // namespace Scripting
