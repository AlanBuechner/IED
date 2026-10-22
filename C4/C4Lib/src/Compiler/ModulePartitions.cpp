#include "Compiler/ModulePartitions.h"
#include "Compiler/PartitionImports.h"
#include "Compiler/PartitionPlacement.h"
#include <llvm/ADT/DenseMap.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

namespace Scripting
{
	namespace
	{
		// No C or C++ symbol can contain a '.', so a foreign object never defines one of these names.
		constexpr const char* kLocalSuffix = ".local";

		// A partition may reach a local symbol another partition defines, so every local becomes a hidden global,
		// renamed first so it cannot meet a foreign object's symbol of the same name in the link.
		void MakeGlobal(llvm::GlobalValue& value)
		{
			if (!value.hasLocalLinkage()) return;
			value.setName(value.getName() + kLocalSuffix);
			value.setLinkage(llvm::GlobalValue::ExternalLinkage);
			value.setVisibility(llvm::GlobalValue::HiddenVisibility);
		}

		void MakeLocalsGlobal(llvm::Module& module)
		{
			for (llvm::Function& function : module) MakeGlobal(function);
			for (llvm::GlobalVariable& variable : module.globals()) MakeGlobal(variable);
			for (llvm::GlobalAlias& alias : module.aliases()) MakeGlobal(alias);
		}

		// An optimizer drops a linkonce definition its own partition does not use, though another partition may.
		void KeepDefinition(llvm::GlobalValue& value)
		{
			if (value.getLinkage() == llvm::GlobalValue::LinkOnceODRLinkage) value.setLinkage(llvm::GlobalValue::WeakODRLinkage);
			else if (value.getLinkage() == llvm::GlobalValue::LinkOnceAnyLinkage) value.setLinkage(llvm::GlobalValue::WeakAnyLinkage);
		}

		void KeepDefinitions(llvm::Module& module)
		{
			for (llvm::Function& function : module) KeepDefinition(function);
			for (llvm::GlobalVariable& variable : module.globals()) KeepDefinition(variable);
		}

		void CopyComdat(llvm::GlobalObject& to, const llvm::GlobalObject& from, llvm::Module& partition)
		{
			if (!from.hasComdat()) return;
			llvm::Comdat* comdat = partition.getOrInsertComdat(from.getComdat()->getName());
			comdat->setSelectionKind(from.getComdat()->getSelectionKind());
			to.setComdat(comdat);
		}

		// Declares, in a partition, what its code reaches that another partition defines: only that, so a partition
		// changes only when its own code or what it reaches does.
		class Declarations : public llvm::ValueMaterializer
		{
		public:
			explicit Declarations(llvm::Module& partition) : m_Partition(partition) {}

			llvm::Value* materialize(llvm::Value* value) override
			{
				const auto* global = llvm::dyn_cast<llvm::GlobalValue>(value);
				if (!global) return nullptr;
				if (global->getValueType()->isFunctionTy()) return DeclareFunction(*global);
				return DeclareVariable(*global);
			}

		private:
			llvm::Function* DeclareFunction(const llvm::GlobalValue& global)
			{
				auto* declared = llvm::Function::Create(llvm::cast<llvm::FunctionType>(global.getValueType()), llvm::GlobalValue::ExternalLinkage,
					global.getAddressSpace(), global.getName(), &m_Partition);
				if (const auto* function = llvm::dyn_cast<llvm::Function>(&global))
				{
					declared->copyAttributesFrom(function);
					declared->setPersonalityFn(nullptr); // a declaration has none
				}
				return declared;
			}

			llvm::GlobalVariable* DeclareVariable(const llvm::GlobalValue& global)
			{
				const auto* variable = llvm::dyn_cast<llvm::GlobalVariable>(&global);
				auto* declared = new llvm::GlobalVariable(m_Partition, global.getValueType(), variable && variable->isConstant(),
					llvm::GlobalValue::ExternalLinkage, nullptr, global.getName(), nullptr, global.getThreadLocalMode(), global.getAddressSpace());
				if (variable) declared->copyAttributesFrom(variable);
				return declared;
			}

			llvm::Module& m_Partition;
		};

		// The definitions are created before any is filled in, so they can refer to one another.
		void CreateShells(const std::vector<const llvm::GlobalValue*>& members, llvm::Module& partition, llvm::ValueToValueMapTy& map)
		{
			for (const llvm::GlobalValue* member : members)
			{
				if (const auto* variable = llvm::dyn_cast<llvm::GlobalVariable>(member))
				{
					auto* shell = new llvm::GlobalVariable(partition, variable->getValueType(), variable->isConstant(), variable->getLinkage(),
						nullptr, variable->getName(), nullptr, variable->getThreadLocalMode(), variable->getAddressSpace());
					shell->copyAttributesFrom(variable);
					map[variable] = shell;
				}
				else if (const auto* function = llvm::dyn_cast<llvm::Function>(member))
				{
					auto* shell = llvm::Function::Create(function->getFunctionType(), function->getLinkage(), function->getAddressSpace(),
						function->getName(), &partition);
					shell->copyAttributesFrom(function);
					map[function] = shell;
				}
				else if (const auto* alias = llvm::dyn_cast<llvm::GlobalAlias>(member))
				{
					auto* shell = llvm::GlobalAlias::create(alias->getValueType(), alias->getAddressSpace(), alias->getLinkage(), alias->getName(), &partition);
					shell->copyAttributesFrom(alias);
					map[alias] = shell;
				}
			}
		}

		void FillVariable(const llvm::GlobalVariable& from, llvm::Module& partition, llvm::ValueToValueMapTy& map, Declarations& declarations)
		{
			auto* to = llvm::cast<llvm::GlobalVariable>(map[&from]);
			if (from.hasInitializer()) to->setInitializer(llvm::MapValue(from.getInitializer(), map, llvm::RF_None, nullptr, &declarations));
			llvm::SmallVector<std::pair<unsigned, llvm::MDNode*>, 2> attachments;
			from.getAllMetadata(attachments);
			for (const auto& [kind, node] : attachments) to->addMetadata(kind, *llvm::MapMetadata(node, map, llvm::RF_None, nullptr, &declarations));
			CopyComdat(*to, from, partition);
		}

		void CloneBody(const llvm::Function& from, llvm::ValueToValueMapTy& map, Declarations& declarations)
		{
			auto* to = llvm::cast<llvm::Function>(map[&from]);
			auto argument = to->arg_begin();
			for (const llvm::Argument& original : from.args())
			{
				argument->setName(original.getName());
				map[&original] = &*argument++;
			}
			llvm::SmallVector<llvm::ReturnInst*, 8> returns;
			llvm::CloneFunctionInto(to, &from, map, llvm::CloneFunctionChangeType::ClonedModule, returns, "", nullptr, nullptr, &declarations);
			if (from.hasPersonalityFn()) to->setPersonalityFn(llvm::MapValue(from.getPersonalityFn(), map, llvm::RF_None, nullptr, &declarations));
		}

		void FillMembers(const std::vector<const llvm::GlobalValue*>& members, llvm::Module& partition, llvm::ValueToValueMapTy& map, Declarations& declarations)
		{
			for (const llvm::GlobalValue* member : members)
			{
				if (const auto* variable = llvm::dyn_cast<llvm::GlobalVariable>(member)) FillVariable(*variable, partition, map, declarations);
				else if (const auto* function = llvm::dyn_cast<llvm::Function>(member))
				{
					CloneBody(*function, map, declarations);
					CopyComdat(*llvm::cast<llvm::Function>(map[function]), *function, partition);
				}
				else if (const auto* alias = llvm::dyn_cast<llvm::GlobalAlias>(member))
					llvm::cast<llvm::GlobalAlias>(map[alias])->setAliasee(llvm::MapValue(alias->getAliasee(), map, llvm::RF_None, nullptr, &declarations));
			}
		}

		// A copy another partition defines: for the optimizer to inline, then drop (no comdat, no DLL storage).
		void CreateImportShells(const std::vector<const llvm::Function*>& imports, llvm::Module& partition, llvm::ValueToValueMapTy& map)
		{
			for (const llvm::Function* import : imports)
			{
				auto* shell = llvm::Function::Create(import->getFunctionType(), llvm::GlobalValue::AvailableExternallyLinkage,
					import->getAddressSpace(), import->getName(), &partition);
				shell->copyAttributesFrom(import);
				shell->setLinkage(llvm::GlobalValue::AvailableExternallyLinkage);
				shell->setDLLStorageClass(llvm::GlobalValue::DefaultStorageClass);
				map[import] = shell;
			}
		}

		// The global variable definition each debug-info global expression is attached to.
		using DescribedGlobals = llvm::DenseMap<const llvm::DIGlobalVariableExpression*, const llvm::GlobalVariable*>;

		DescribedGlobals DescribedDefinitions(const llvm::Module& module)
		{
			DescribedGlobals described;
			llvm::SmallVector<llvm::DIGlobalVariableExpression*, 1> expressions;
			for (const llvm::GlobalVariable& variable : module.globals())
			{
				if (variable.isDeclaration()) continue;
				expressions.clear();
				variable.getDebugInfo(expressions);
				for (const llvm::DIGlobalVariableExpression* expression : expressions) described[expression] = &variable;
			}
			return described;
		}

		// A debug-info compile unit and the partition's own copy of it, whose lists are filled in after its code.
		struct UnitCopy
		{
			const llvm::DICompileUnit* from;
			llvm::DICompileUnit* to;
		};

		// Made before any code is cloned, so the partition's code refers to its own copy, without the program's lists.
		std::vector<UnitCopy> CreateUnits(const llvm::Module& module, llvm::ValueToValueMapTy& map)
		{
			std::vector<UnitCopy> units;
			for (const llvm::DICompileUnit* unit : module.debug_compile_units())
			{
				llvm::TempDICompileUnit copy = unit->clone();
				copy->replaceEnumTypes({});
				copy->replaceRetainedTypes({});
				copy->replaceGlobalVariables({});
				copy->replaceImportedEntities({});
				copy->replaceMacros({});
				llvm::DICompileUnit* own = llvm::MDNode::replaceWithDistinct(std::move(copy));
				map.MD()[unit].reset(own);
				units.push_back({ unit, own });
			}
			return units;
		}

		llvm::MDTuple* MappedList(const llvm::MDTuple* list, llvm::ValueToValueMapTy& map, Declarations& declarations)
		{
			return list ? llvm::cast<llvm::MDTuple>(llvm::MapMetadata(list, map, llvm::RF_None, nullptr, &declarations)) : nullptr;
		}

		bool DefinedHere(const llvm::GlobalVariable& variable, const llvm::ValueToValueMapTy& map)
		{
			const auto mapped = map.find(&variable);
			return mapped != map.end() && !llvm::cast<llvm::GlobalValue>(mapped->second)->isDeclaration();
		}

		// One partition: its members, then copies of the functions it imports.
		struct Contents
		{
			std::vector<const llvm::GlobalValue*> members;
			std::vector<const llvm::Function*> imports;
			bool keepsProgramWide = false; // the file-scope assembly and the debug info's program-wide lists
		};

		// The expressions of the globals the partition defines; the partition that keeps the program-wide lists also
		// gets those no definition carries.
		llvm::MDTuple* ListedGlobals(const llvm::DICompileUnit& unit, const Contents& contents, const DescribedGlobals& described,
			llvm::ValueToValueMapTy& map, Declarations& declarations)
		{
			std::vector<llvm::Metadata*> listed;
			for (const llvm::DIGlobalVariableExpression* expression : unit.getGlobalVariables())
			{
				const llvm::GlobalVariable* carrier = described.lookup(expression);
				if (carrier ? DefinedHere(*carrier, map) : contents.keepsProgramWide)
					listed.push_back(llvm::MapMetadata(expression, map, llvm::RF_None, nullptr, &declarations));
			}
			return llvm::MDTuple::get(unit.getContext(), listed);
		}

		// A partition's unit lists only the globals it defines, so changing a global changes only the partition that
		// defines it (BUG-327).
		void FillUnit(const UnitCopy& unit, const Contents& contents, const DescribedGlobals& described, llvm::ValueToValueMapTy& map,
			Declarations& declarations)
		{
			unit.to->replaceGlobalVariables(ListedGlobals(*unit.from, contents, described, map, declarations));
			if (!contents.keepsProgramWide) return;
			unit.to->replaceEnumTypes(MappedList(unit.from->getEnumTypes().get(), map, declarations));
			unit.to->replaceRetainedTypes(MappedList(unit.from->getRetainedTypes().get(), map, declarations));
			unit.to->replaceImportedEntities(MappedList(unit.from->getImportedEntities().get(), map, declarations));
			unit.to->replaceMacros(MappedList(unit.from->getMacros().get(), map, declarations));
		}

		// Module flags, the debug-info compile unit and the rest of the named metadata go to every partition.
		void CopyNamedMetadata(const llvm::Module& module, llvm::Module& partition, llvm::ValueToValueMapTy& map, Declarations& declarations)
		{
			for (const llvm::NamedMDNode& named : module.named_metadata())
			{
				llvm::NamedMDNode* copy = partition.getOrInsertNamedMetadata(named.getName());
				for (const llvm::MDNode* node : named.operands()) copy->addOperand(llvm::MapMetadata(node, map, llvm::RF_None, nullptr, &declarations));
			}
		}

		std::unique_ptr<llvm::Module> Partition(const llvm::Module& module, const Contents& contents, const DescribedGlobals& described)
		{
			auto partition = std::make_unique<llvm::Module>(module.getModuleIdentifier(), module.getContext());
			partition->setSourceFileName(module.getSourceFileName());
			partition->setDataLayout(module.getDataLayout());
			partition->setTargetTriple(module.getTargetTriple());
			if (contents.keepsProgramWide) partition->setModuleInlineAsm(module.getModuleInlineAsm());
			llvm::ValueToValueMapTy map;
			Declarations declarations(*partition);
			const std::vector<UnitCopy> units = CreateUnits(module, map);
			CreateShells(contents.members, *partition, map);
			CreateImportShells(contents.imports, *partition, map);
			FillMembers(contents.members, *partition, map, declarations);
			for (const llvm::Function* import : contents.imports) CloneBody(*import, map, declarations);
			for (const UnitCopy& unit : units) FillUnit(unit, contents, described, map, declarations);
			CopyNamedMetadata(module, *partition, map, declarations);
			return partition;
		}
	}

	std::unique_ptr<llvm::Module> ExtractDefinitions(const llvm::Module& module, const std::vector<const llvm::GlobalValue*>& definitions)
	{
		return Partition(module, Contents{ definitions, {}, false }, DescribedDefinitions(module));
	}

	std::string ModuleBitcode(const llvm::Module& module)
	{
		std::string bitcode;
		llvm::raw_string_ostream out(bitcode);
		llvm::WriteBitcodeToFile(module, out);
		out.flush();
		return bitcode;
	}

	std::vector<std::string> SplitModuleToBitcode(llvm::Module& module, unsigned count, PartitionOptimization optimization)
	{
		// Assigned before the locals are renamed, so a symbol's home is found by the names the program gave it.
		const std::vector<std::vector<const llvm::GlobalValue*>> members = AssignDefinitions(module, count);
		const DescribedGlobals described = DescribedDefinitions(module);
		MakeLocalsGlobal(module);
		const bool after = optimization == PartitionOptimization::After;
		if (after) KeepDefinitions(module);
		std::vector<std::string> partitions;
		for (unsigned i = 0; i < count; ++i)
		{
			const Contents contents{ members[i], after ? ImportsFor(members[i]) : std::vector<const llvm::Function*>(), i == 0 };
			partitions.push_back(ModuleBitcode(*Partition(module, contents, described)));
		}
		return partitions;
	}
}
