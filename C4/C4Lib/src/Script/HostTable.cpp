#include "Script/HostTable.h"
#include "Compiler/ModulePartitions.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/Error.h>
#include <tuple>
#include <unordered_set>

namespace Scripting
{
	namespace
	{
		constexpr const char* kTableSymbol = "__c4_script_table";
		constexpr const char* kBuildIdSymbol = "__c4_script_build_id";
		constexpr const char* kAccessorPrefix = "__c4_tls_";

		// The module's own tables: the runtime reads them from the host image, a script has its own.
		bool OwnTable(llvm::StringRef name)
		{
			return name == "__c4_reflect_image" || name == "__c4_gc_roots" || name == "__c4_gc_thread_root_count" || name == kTableSymbol ||
				name == kBuildIdSymbol || name == "__ImageBase";
		}

		llvm::ArrayType* BuildIdType(llvm::LLVMContext& context)
		{
			return llvm::ArrayType::get(llvm::Type::getInt8Ty(context), std::tuple_size_v<std::array<uint8, 16>>);
		}

		llvm::GlobalVariable* BuildIdIn(llvm::Module& module)
		{
			if (llvm::GlobalVariable* id = module.getNamedGlobal(kBuildIdSymbol)) return id;
			return new llvm::GlobalVariable(module, BuildIdType(module.getContext()), true, llvm::GlobalValue::ExternalLinkage, nullptr,
				kBuildIdSymbol);
		}

		bool ListedFunction(const llvm::Function& f, const std::unordered_set<std::string>& always)
		{
			if (f.isIntrinsic() || !f.hasName()) return false;
			return !f.isDeclaration() || !f.use_empty() || always.count(f.getName().str());
		}

		// A type descriptor is not listed: RTTI and exception tables name theirs image-relatively, so a script makes its
		// own, which matches the host's by name.
		bool IsTypeDescriptor(llvm::StringRef name) { return name.starts_with("??_R0"); }

		bool ListedGlobal(const llvm::GlobalVariable& g, const std::unordered_set<std::string>& always)
		{
			if (!g.hasName() || g.getName().starts_with("llvm.") || g.isThreadLocal() || g.hasPrivateLinkage() || OwnTable(g.getName()) ||
				IsTypeDescriptor(g.getName()))
				return false;
			return !g.isDeclaration() || !g.use_empty() || always.count(g.getName().str());
		}

		// `value` of another partition, declared in `module` when it does not have the name.
		void DeclareIn(llvm::Module& module, const llvm::GlobalValue& value)
		{
			if (value.getParent() == &module || module.getNamedValue(value.getName())) return;
			llvm::GlobalValue* declared = nullptr;
			if (llvm::isa<llvm::Function>(value))
				declared = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(module.getContext()), false),
					llvm::GlobalValue::ExternalLinkage, value.getName(), module);
			else
				declared = new llvm::GlobalVariable(module, llvm::Type::getInt8Ty(module.getContext()), false, llvm::GlobalValue::ExternalLinkage,
					nullptr, value.getName());
			declared->setVisibility(value.getVisibility());
		}

		// A declaration for a name the table lists although the module never declared it; the table needs only the
		// address, so the signature does not matter.
		void DeclareIfMissing(llvm::Module& module, const std::string& name)
		{
			if (module.getNamedValue(name)) return;
			auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(module.getContext()), false);
			llvm::Function::Create(type, llvm::GlobalValue::ExternalLinkage, name, module);
		}
	}

	const std::vector<const char*>& ScriptRuntimeEntryPoints()
	{
		static const std::vector<const char*> kNames = {
			// what codegen calls (Backend/CodeGen)
			"c4_alloc", "c4_alloc_noscan", "c4_alloc_typed", "c4_alloc_typed_noscan", "c4_free", "c4_gc_register_finalizer", "c4_dynamic_cast", "c4_instance_of", "c4_purecall",
			"c4_noexcept_violation", "c4_once_begin", "c4_once_end", "c4_once_abandon", "c4_thread_once_abandon", "c4_thread_once_reentered",
			"c4_reflect_register_image", "__CxxFrameHandler3",
			// the test harness and assertions, for a script's tests
			"c4_test_configure", "c4_test_selected", "c4_test_begin", "c4_test_now_ns", "c4_test_run_case", "c4_test_case_done",
			"c4_test_summary", "c4_test_fail_count", "c4_assert_true_at", "c4_assert_eq_at", "c4_assert_feq_at", "c4_assert_se_at",
			// script support (C4Runtime/ScriptRuntimeC.h) and what the script host asks of the GC and reflection
			"c4_script_module_create", "c4_script_at_exit", "c4_script_thread_at_exit", "c4_script_module_finish",
			"c4_script_module_destroy", "__emutls_get_address", "c4_script_tls_release", "c4_script_image_add", "c4_script_image_remove",
			"c4_script_throw", "c4_gc_add_root_table", "c4_gc_remove_root_table", "c4_gc_drop_finalizers_in", "c4_gc_forget_types_in", "c4_reflect_unload_source",
			"c4_reflect_notify_unload", "c4_reflect_remove_source",
		};
		return kNames;
	}

	void EmitThreadLocalAccessors(llvm::Module& module)
	{
		std::vector<llvm::GlobalVariable*> threadLocals;
		for (llvm::GlobalVariable& g : module.globals())
			if (g.isThreadLocal() && !g.isDeclaration() && g.hasName()) threadLocals.push_back(&g);
		auto* type = llvm::FunctionType::get(llvm::PointerType::get(module.getContext(), 0), false);
		for (llvm::GlobalVariable* g : threadLocals)
		{
			auto* accessor = llvm::Function::Create(type, llvm::GlobalValue::ExternalLinkage, kAccessorPrefix + ProgramName(*g), module);
			llvm::IRBuilder<> B(llvm::BasicBlock::Create(module.getContext(), "entry", accessor));
			B.CreateRet(B.CreateThreadLocalAddress(g));
		}
	}

	std::vector<std::string> HostSymbols(llvm::Module& module, const std::vector<std::string>& alsoListed)
	{
		return HostSymbols(std::vector<llvm::Module*>{ &module }, alsoListed);
	}

	// The first partition declares what the others define, so its table can take each one's address: a function as
	// `void()`, anything else as a byte; a local of the program hidden, as its definition is. Another partition's own
	// locals (a census counter, a vtable's copy) are its helpers, which no other partition can name.
	std::vector<std::string> HostSymbols(const std::vector<llvm::Module*>& partitions, const std::vector<std::string>& alsoListed)
	{
		llvm::Module& first = *partitions.front();
		std::unordered_set<std::string> always(alsoListed.begin(), alsoListed.end());
		for (const char* name : ScriptRuntimeEntryPoints()) always.insert(name);
		for (const char* name : ScriptRuntimeEntryPoints()) DeclareIfMissing(first, name); // in a fixed order: the table's
		for (const std::string& name : alsoListed) DeclareIfMissing(first, name);
		std::vector<std::string> names;
		std::unordered_set<std::string> seen;
		for (llvm::Module* partition : partitions)
		{
			const bool isFirst = partition == &first;
			for (const llvm::Function& f : *partition)
				if (ListedFunction(f, always) && (isFirst || !f.hasLocalLinkage()) && seen.insert(ProgramName(f)).second)
				{
					names.push_back(ProgramName(f));
					DeclareIn(first, f);
				}
			for (const llvm::GlobalVariable& g : partition->globals())
				if (ListedGlobal(g, always) && (isFirst || !g.hasLocalLinkage()) && seen.insert(ProgramName(g)).second)
				{
					names.push_back(ProgramName(g));
					DeclareIn(first, g);
				}
		}
		return names;
	}

	std::optional<std::vector<std::string>> ForeignDefinitions(const std::vector<std::string>& objectPaths, std::string* error)
	{
		using llvm::object::SymbolRef;
		std::vector<std::string> names;
		for (const std::string& path : objectPaths)
		{
			llvm::Expected<llvm::object::OwningBinary<llvm::object::ObjectFile>> object = llvm::object::ObjectFile::createObjectFile(path);
			if (!object)
			{
				if (error) *error = "cannot read the foreign object '" + path + "': " + llvm::toString(object.takeError());
				return std::nullopt;
			}
			for (const SymbolRef& symbol : object->getBinary()->symbols())
			{
				llvm::Expected<uint32_t> flags = symbol.getFlags();
				llvm::Expected<llvm::StringRef> name = symbol.getName();
				if (!flags || !name)
				{
					if (!flags) llvm::consumeError(flags.takeError());
					if (!name) llvm::consumeError(name.takeError());
					continue;
				}
				const bool defined = !(*flags & SymbolRef::SF_Undefined) && (*flags & SymbolRef::SF_Global) && !(*flags & SymbolRef::SF_FormatSpecific);
				if (defined && !name->empty()) names.push_back(name->str());
			}
		}
		return names;
	}

	bool EmitHostTable(llvm::Module& module, const std::vector<std::string>& symbols, std::string* error)
	{
		llvm::LLVMContext& context = module.getContext();
		llvm::Type* ptr = llvm::PointerType::get(context, 0);
		std::vector<llvm::Constant*> addresses;
		for (const std::string& name : symbols)
		{
			llvm::GlobalValue* value = module.getNamedValue(name);
			if (!value) value = module.getNamedValue(name + kLocalSuffix); // a split program's local: hidden under its suffix
			if (!value)
			{
				if (error) *error = "the script host table names '" + name + "', which the program does not have";
				return false;
			}
			addresses.push_back(value);
		}
		auto* entriesType = llvm::ArrayType::get(ptr, addresses.size());
		auto* tableType = llvm::StructType::get(context, { ptr, llvm::Type::getInt64Ty(context), entriesType });
		llvm::Constant* table = llvm::ConstantStruct::get(tableType, {
			BuildIdIn(module),
			llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), addresses.size()),
			llvm::ConstantArray::get(entriesType, addresses) });
		auto* global = new llvm::GlobalVariable(module, tableType, true, llvm::GlobalValue::ExternalLinkage, table, "__c4_script_table.new");
		if (llvm::GlobalVariable* declared = module.getNamedGlobal(kTableSymbol))
		{
			declared->replaceAllUsesWith(global);
			declared->eraseFromParent();
		}
		global->setName(kTableSymbol);
		return true;
	}

	void EmitBuildId(llvm::Module& module, const std::array<uint8, 16>& buildId)
	{
		BuildIdIn(module)->setInitializer(llvm::ConstantDataArray::get(module.getContext(), llvm::ArrayRef<uint8_t>(buildId.data(), buildId.size())));
	}
} // namespace Scripting
