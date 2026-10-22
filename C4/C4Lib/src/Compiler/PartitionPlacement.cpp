#include "Compiler/PartitionPlacement.h"
#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/xxhash.h>
#include <algorithm>
#include <bit>
#include <string>

namespace Scripting
{
	namespace
	{
		// How far a private symbol is followed to the code that uses it (a table of tables of strings).
		constexpr int kMaxHomeHops = 8;
		// A file's code takes one partition per this many instructions, rounded down to a power of two, so its share
		// changes only when the file doubles or halves.
		constexpr uint64_t kInstructionsPerFileShare = 8000;

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
		// tables with the image that lists them. Code, private or not, is placed by itself: following uses would gather
		// every reflection thunk into one partition.
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

		// The definition a comdat is named after, which places the whole group.
		const llvm::GlobalValue& Leader(const llvm::GlobalValue& member)
		{
			const llvm::GlobalValue* leader = member.getParent()->getNamedValue(member.getComdat()->getName());
			return leader ? *leader : member;
		}

		// The source file a definition's debug info names, or null.
		const llvm::DIFile* SourceFileOf(const llvm::GlobalValue& value)
		{
			if (const auto* function = llvm::dyn_cast<llvm::Function>(&value))
				return function->getSubprogram() ? function->getSubprogram()->getFile() : nullptr;
			const auto* variable = llvm::dyn_cast<llvm::GlobalVariable>(&value);
			if (!variable) return nullptr;
			llvm::SmallVector<llvm::DIGlobalVariableExpression*, 1> expressions;
			variable->getDebugInfo(expressions);
			return expressions.empty() ? nullptr : expressions.front()->getVariable()->getFile();
		}

		uint64_t FileHash(const llvm::DIFile& file)
		{
			return llvm::xxh3_64bits((file.getDirectory() + "/" + file.getFilename()).str());
		}

		class Placement
		{
		public:
			Placement(const llvm::Module& module, unsigned count) : m_Count(count)
			{
				for (const llvm::Function& function : module)
					if (const llvm::DISubprogram* subprogram = function.getSubprogram())
						m_FileInstructions[subprogram->getFile()] += function.getInstructionCount();
			}

			// Data no source file names (the reflection image and its rows, the GC roots table) in the first partition, so
			// an edit that changes it reassembles data and no code. Code and the rest by the home's file, over the file's
			// share of the other partitions by name; by name alone without debug info. A comdat goes by its name and its
			// leader's file. A definition keeps its partition while the program changes around it.
			unsigned PartitionOf(const llvm::GlobalValue& value) const
			{
				if (const auto* alias = llvm::dyn_cast<llvm::GlobalAlias>(&value))
					if (const llvm::GlobalObject* base = alias->getAliaseeObject()) return PartitionOf(*base);
				const llvm::GlobalValue& home = value.hasComdat() ? value : Home(value);
				const llvm::StringRef key = home.hasComdat() ? home.getComdat()->getName() : home.getName();
				const llvm::DIFile* file = SourceFileOf(home.hasComdat() ? Leader(home) : home);
				if (m_Count == 1 || (!file && !llvm::isa<llvm::Function>(home))) return 0;
				const uint64_t byName = llvm::xxh3_64bits(key);
				const uint64_t others = m_Count - 1;
				if (!file) return static_cast<unsigned>(1 + byName % others);
				return static_cast<unsigned>(1 + (FileHash(*file) + byName % Share(*file)) % others);
			}

		private:
			uint64_t Share(const llvm::DIFile& file) const
			{
				const auto found = m_FileInstructions.find(&file);
				const uint64_t instructions = found == m_FileInstructions.end() ? 0 : found->second;
				return std::bit_floor(std::clamp<uint64_t>(instructions / kInstructionsPerFileShare, 1, m_Count - 1));
			}

			unsigned m_Count;
			llvm::DenseMap<const llvm::DIFile*, uint64_t> m_FileInstructions;
		};
	}

	std::vector<std::vector<const llvm::GlobalValue*>> AssignDefinitions(const llvm::Module& module, unsigned count)
	{
		const Placement placement(module, count);
		std::vector<std::vector<const llvm::GlobalValue*>> members(count);
		for (const llvm::GlobalVariable& variable : module.globals())
			if (!variable.isDeclaration()) members[placement.PartitionOf(variable)].push_back(&variable);
		for (const llvm::Function& function : module)
			if (!function.isDeclaration()) members[placement.PartitionOf(function)].push_back(&function);
		for (const llvm::GlobalAlias& alias : module.aliases()) members[placement.PartitionOf(alias)].push_back(&alias);
		return members;
	}
}
