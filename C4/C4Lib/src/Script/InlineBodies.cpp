#include "Script/InlineBodies.h"
#include "Compiler/FunctionSize.h"
#include "Compiler/ModulePartitions.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Scripting
{
	namespace
	{
		// A private constant a body names is copied with it: a script makes its own.
		bool IsCopied(const llvm::GlobalVariable& variable)
		{
			return variable.hasPrivateLinkage() && variable.isConstant() && variable.hasInitializer();
		}

		// Decides what a body may name: a table symbol, an intrinsic, or a private constant whose own initializer
		// names only those (it is copied). Remembers each answer.
		class Reach
		{
		public:
			explicit Reach(const HostNames& host) : m_Host(host) {}

			bool FunctionQualifies(const llvm::Function& f)
			{
				if (f.isDeclaration() || !HasAtMostInstructions(f, kInlineBodyLimit) || !m_Host.Lists(f)) return false;
				for (const llvm::BasicBlock& block : f)
					for (const llvm::Instruction& instruction : block)
						for (const llvm::Use& operand : instruction.operands())
							if (!Allowed(operand.get())) return false;
				return !f.hasPersonalityFn() || Allowed(f.getPersonalityFn());
			}

		private:
			bool Allowed(const llvm::Value* value)
			{
				if (const auto* global = llvm::dyn_cast<llvm::GlobalValue>(value)) return GlobalAllowed(*global);
				const auto* constant = llvm::dyn_cast<llvm::Constant>(value);
				if (!constant || llvm::isa<llvm::ConstantData>(constant)) return true;
				for (const llvm::Use& operand : constant->operands())
					if (!Allowed(operand.get())) return false;
				return true;
			}

			bool GlobalAllowed(const llvm::GlobalValue& global)
			{
				if (const auto* function = llvm::dyn_cast<llvm::Function>(&global))
					return function->isIntrinsic() || m_Host.Lists(*function);
				const auto* variable = llvm::dyn_cast<llvm::GlobalVariable>(&global);
				if (!variable || variable->isThreadLocal()) return false;
				if (variable->getName() == "__ImageBase" || m_Host.Lists(*variable)) return true;
				if (!IsCopied(*variable)) return false;
				auto [known, fresh] = m_Constants.try_emplace(variable, true);
				if (!fresh) return known->second; // a cycle through constants (an RTTI table and its descriptor) is fine
				known->second = Allowed(variable->getInitializer());
				return known->second;
			}

			const HostNames& m_Host;
			std::unordered_map<const llvm::GlobalVariable*, bool> m_Constants;
		};

		// The private constants `value` names, and those their initializers name.
		void AddConstants(const llvm::Value* value, std::unordered_set<const llvm::GlobalVariable*>& constants)
		{
			if (const auto* variable = llvm::dyn_cast<llvm::GlobalVariable>(value))
			{
				if (IsCopied(*variable) && constants.insert(variable).second) AddConstants(variable->getInitializer(), constants);
				return;
			}
			const auto* constant = llvm::dyn_cast<llvm::Constant>(value);
			if (!constant || llvm::isa<llvm::GlobalValue>(constant) || llvm::isa<llvm::ConstantData>(constant)) return;
			for (const llvm::Use& operand : constant->operands()) AddConstants(operand.get(), constants);
		}

		// `functions` after the constants they name, in the module's order.
		std::vector<const llvm::GlobalValue*> Group(const llvm::Module& module, const llvm::Function* const* functions, size_t count)
		{
			std::unordered_set<const llvm::GlobalVariable*> constants;
			for (size_t i = 0; i < count; ++i)
			{
				for (const llvm::BasicBlock& block : *functions[i])
					for (const llvm::Instruction& instruction : block)
						for (const llvm::Use& operand : instruction.operands()) AddConstants(operand.get(), constants);
				if (functions[i]->hasPersonalityFn()) AddConstants(functions[i]->getPersonalityFn(), constants);
			}
			std::vector<const llvm::GlobalValue*> group;
			for (const llvm::GlobalVariable& g : module.globals())
				if (constants.count(&g)) group.push_back(&g);
			group.insert(group.end(), functions, functions + count);
			return group;
		}
	}

	bool HostNames::Lists(const llvm::GlobalValue& value) const
	{
		return m_Names.count(ProgramName(value)) != 0;
	}

	std::vector<std::vector<const llvm::GlobalValue*>> InlineBodyGroups(const llvm::Module& module, const HostNames& hostSymbols,
		unsigned maxGroups)
	{
		Reach reach(hostSymbols);
		std::vector<const llvm::Function*> functions;
		for (const llvm::Function& f : module)
			if (reach.FunctionQualifies(f)) functions.push_back(&f);
		std::vector<std::vector<const llvm::GlobalValue*>> grouped;
		const size_t count = std::clamp<size_t>(functions.size() / kInlineBodiesPerGroup, 1, std::max(maxGroups, 1u));
		for (size_t i = 0; i < count; ++i)
		{
			const size_t first = functions.size() * i / count, last = functions.size() * (i + 1) / count;
			if (first < last) grouped.push_back(Group(module, functions.data() + first, last - first));
		}
		return grouped;
	}

	// A split program's locals take the names the table lists them by.
	std::string InlineBodyBitcode(llvm::Module& group)
	{
		for (llvm::GlobalValue& value : group.global_values())
			if (!value.hasLocalLinkage())
				if (const std::string name = ProgramName(value).str(); name != value.getName())
				{
					value.setName(name);
					value.setVisibility(llvm::GlobalValue::DefaultVisibility);
				}
		for (llvm::Function& f : group)
			if (!f.isDeclaration()) f.setLinkage(llvm::GlobalValue::ExternalLinkage); // named as the table names it
		llvm::StripDebugInfo(group);
		return ModuleBitcode(group);
	}

	std::vector<std::string> InlineBodies(const llvm::Module& module, const HostNames& hostSymbols, unsigned maxGroups)
	{
		std::vector<std::string> bodies;
		for (const std::vector<const llvm::GlobalValue*>& group : InlineBodyGroups(module, hostSymbols, maxGroups))
			bodies.push_back(InlineBodyBitcode(*ExtractDefinitions(module, group)));
		return bodies;
	}

	std::vector<std::string> InlineBodies(const llvm::Module& module, const std::vector<std::string>& hostSymbols, unsigned maxGroups)
	{
		return InlineBodies(module, HostNames(hostSymbols), maxGroups);
	}
} // namespace Scripting
