#include "Compiler/ModulePartitions.h"
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/xxhash.h>
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

		// How far a private symbol is followed to the code that uses it (a table of tables of strings).
		constexpr int kMaxHomeHops = 8;

		// The global a use belongs to: the function an instruction is in, the global a constant initializes.
		void Owners(const llvm::User& user, std::vector<const llvm::GlobalValue*>& owners)
		{
			if (const auto* instruction = llvm::dyn_cast<llvm::Instruction>(&user)) { owners.push_back(instruction->getFunction()); return; }
			if (const auto* global = llvm::dyn_cast<llvm::GlobalValue>(&user)) { owners.push_back(global); return; }
			for (const llvm::User* outer : user.users()) Owners(*outer, owners);
		}

		// Of a private symbol's users, the one whose name sorts first: which user that is does not depend on the order
		// the uses were made in.
		const llvm::GlobalValue* FirstOwner(const llvm::GlobalValue& value)
		{
			std::vector<const llvm::GlobalValue*> owners;
			for (const llvm::User* user : value.users()) Owners(*user, owners);
			const llvm::GlobalValue* first = nullptr;
			for (const llvm::GlobalValue* owner : owners)
				if (owner != &value && (!first || owner->getName() < first->getName())) first = owner;
			return first;
		}

		// Private data lives with the code that uses it, so a literal changes only with its function and the reflection
		// tables with the image that lists them. Code, private or not, is spread by its own name: following uses would
		// gather every reflection thunk into one partition.
		bool FollowsItsUser(const llvm::GlobalValue& value)
		{
			return value.hasLocalLinkage() && llvm::isa<llvm::GlobalVariable>(value);
		}

		const llvm::GlobalValue& Home(const llvm::GlobalValue& value)
		{
			const llvm::GlobalValue* home = &value;
			for (int hop = 0; hop < kMaxHomeHops && FollowsItsUser(*home); ++hop)
			{
				const llvm::GlobalValue* owner = FirstOwner(*home);
				if (!owner) break;
				home = owner;
			}
			return *home;
		}

		// By its home's name, or by the comdat's so a group stays together: a definition keeps its partition while the
		// program changes around it.
		unsigned PartitionOf(const llvm::GlobalValue& value, unsigned count)
		{
			if (const auto* alias = llvm::dyn_cast<llvm::GlobalAlias>(&value))
				if (const llvm::GlobalObject* base = alias->getAliaseeObject()) return PartitionOf(*base, count);
			const llvm::GlobalValue& home = value.hasComdat() ? value : Home(value);
			const llvm::StringRef key = home.hasComdat() ? home.getComdat()->getName() : home.getName();
			return static_cast<unsigned>(llvm::xxh3_64bits(key) % count);
		}

		// Assigned before the locals are renamed, so a symbol's home is found by the names the program gave it.
		std::vector<std::vector<const llvm::GlobalValue*>> AssignDefinitions(const llvm::Module& module, unsigned count)
		{
			std::vector<std::vector<const llvm::GlobalValue*>> members(count);
			for (const llvm::GlobalVariable& variable : module.globals())
				if (!variable.isDeclaration()) members[PartitionOf(variable, count)].push_back(&variable);
			for (const llvm::Function& function : module)
				if (!function.isDeclaration()) members[PartitionOf(function, count)].push_back(&function);
			for (const llvm::GlobalAlias& alias : module.aliases()) members[PartitionOf(alias, count)].push_back(&alias);
			return members;
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

		void FillFunction(const llvm::Function& from, llvm::Module& partition, llvm::ValueToValueMapTy& map, Declarations& declarations)
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
			CopyComdat(*to, from, partition);
		}

		void FillMembers(const std::vector<const llvm::GlobalValue*>& members, llvm::Module& partition, llvm::ValueToValueMapTy& map, Declarations& declarations)
		{
			for (const llvm::GlobalValue* member : members)
			{
				if (const auto* variable = llvm::dyn_cast<llvm::GlobalVariable>(member)) FillVariable(*variable, partition, map, declarations);
				else if (const auto* function = llvm::dyn_cast<llvm::Function>(member)) FillFunction(*function, partition, map, declarations);
				else if (const auto* alias = llvm::dyn_cast<llvm::GlobalAlias>(member))
					llvm::cast<llvm::GlobalAlias>(map[alias])->setAliasee(llvm::MapValue(alias->getAliasee(), map, llvm::RF_None, nullptr, &declarations));
			}
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

		std::unique_ptr<llvm::Module> Partition(const llvm::Module& module, const std::vector<const llvm::GlobalValue*>& members, bool keepsAssembly)
		{
			auto partition = std::make_unique<llvm::Module>(module.getModuleIdentifier(), module.getContext());
			partition->setSourceFileName(module.getSourceFileName());
			partition->setDataLayout(module.getDataLayout());
			partition->setTargetTriple(module.getTargetTriple());
			if (keepsAssembly) partition->setModuleInlineAsm(module.getModuleInlineAsm());
			llvm::ValueToValueMapTy map;
			Declarations declarations(*partition);
			CreateShells(members, *partition, map);
			FillMembers(members, *partition, map, declarations);
			CopyNamedMetadata(module, *partition, map, declarations);
			return partition;
		}
	}

	std::string ModuleBitcode(const llvm::Module& module)
	{
		std::string bitcode;
		llvm::raw_string_ostream out(bitcode);
		llvm::WriteBitcodeToFile(module, out);
		out.flush();
		return bitcode;
	}

	std::vector<std::string> SplitModuleToBitcode(llvm::Module& module, unsigned count)
	{
		const std::vector<std::vector<const llvm::GlobalValue*>> members = AssignDefinitions(module, count);
		MakeLocalsGlobal(module);
		std::vector<std::string> partitions;
		for (unsigned i = 0; i < count; ++i) partitions.push_back(ModuleBitcode(*Partition(module, members[i], i == 0)));
		return partitions;
	}
}
