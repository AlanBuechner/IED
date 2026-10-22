#include "Script/InlineBodies.h"
#include "Compiler/FunctionSize.h"
#include "Compiler/ModulePartitions.h"
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <unordered_map>
#include <unordered_set>

namespace Scripting
{
	namespace
	{
		// Decides what a body may name: a table symbol, an intrinsic, or a private constant whose own initializer
		// names only those (it is copied). Remembers each answer, and which constants the chosen bodies need.
		class Reach
		{
		public:
			explicit Reach(const std::vector<std::string>& hostSymbols) : m_Host(hostSymbols.begin(), hostSymbols.end()) {}

			bool FunctionQualifies(const llvm::Function& f)
			{
				if (f.isDeclaration() || !HasAtMostInstructions(f, kInlineBodyLimit) || !m_Host.count(f.getName().str())) return false;
				std::vector<const llvm::GlobalVariable*> constants;
				for (const llvm::BasicBlock& block : f)
					for (const llvm::Instruction& instruction : block)
						for (const llvm::Use& operand : instruction.operands())
							if (!Allowed(operand.get(), constants)) return false;
				if (f.hasPersonalityFn() && !Allowed(f.getPersonalityFn(), constants)) return false;
				m_Copied.insert(constants.begin(), constants.end());
				return true;
			}

			bool Copied(const llvm::GlobalValue* value) const
			{
				return m_Copied.count(llvm::dyn_cast<llvm::GlobalVariable>(value)) != 0;
			}

		private:
			bool Allowed(const llvm::Value* value, std::vector<const llvm::GlobalVariable*>& constants)
			{
				if (const auto* global = llvm::dyn_cast<llvm::GlobalValue>(value)) return GlobalAllowed(*global, constants);
				const auto* constant = llvm::dyn_cast<llvm::Constant>(value);
				if (!constant || llvm::isa<llvm::ConstantData>(constant)) return true;
				for (const llvm::Use& operand : constant->operands())
					if (!Allowed(operand.get(), constants)) return false;
				return true;
			}

			bool GlobalAllowed(const llvm::GlobalValue& global, std::vector<const llvm::GlobalVariable*>& constants)
			{
				if (const auto* function = llvm::dyn_cast<llvm::Function>(&global))
					return function->isIntrinsic() || m_Host.count(function->getName().str());
				const auto* variable = llvm::dyn_cast<llvm::GlobalVariable>(&global);
				if (!variable || variable->isThreadLocal()) return false;
				if (variable->getName() == "__ImageBase" || m_Host.count(variable->getName().str())) return true;
				if (!variable->hasPrivateLinkage() || !variable->isConstant() || !variable->hasInitializer()) return false;
				auto [known, fresh] = m_Constants.try_emplace(variable, true);
				if (!fresh) // a cycle through constants (an RTTI table and its descriptor) is fine
				{
					if (known->second) constants.push_back(variable);
					return known->second;
				}
				known->second = Allowed(variable->getInitializer(), constants);
				if (known->second) constants.push_back(variable);
				return known->second;
			}

			std::unordered_set<std::string> m_Host;
			std::unordered_map<const llvm::GlobalVariable*, bool> m_Constants;
			std::unordered_set<const llvm::GlobalVariable*> m_Copied;
		};

		// The qualifying functions and the constants they need, in the module's order.
		std::vector<const llvm::GlobalValue*> Definitions(const llvm::Module& module, const std::vector<std::string>& hostSymbols)
		{
			Reach reach(hostSymbols);
			std::vector<const llvm::GlobalValue*> functions;
			for (const llvm::Function& f : module)
				if (reach.FunctionQualifies(f)) functions.push_back(&f);
			std::vector<const llvm::GlobalValue*> definitions;
			for (const llvm::GlobalVariable& g : module.globals())
				if (reach.Copied(&g)) definitions.push_back(&g);
			definitions.insert(definitions.end(), functions.begin(), functions.end());
			return definitions;
		}
	}

	std::string InlineBodies(const llvm::Module& module, const std::vector<std::string>& hostSymbols)
	{
		const std::vector<const llvm::GlobalValue*> definitions = Definitions(module, hostSymbols);
		if (definitions.empty()) return {};
		std::unique_ptr<llvm::Module> copy = ExtractDefinitions(module, definitions);
		for (llvm::Function& f : *copy)
			if (!f.isDeclaration()) f.setLinkage(llvm::GlobalValue::ExternalLinkage); // named as the table names it
		llvm::StripDebugInfo(*copy);

		std::string bitcode;
		llvm::raw_string_ostream out(bitcode);
		llvm::WriteBitcodeToFile(*copy, out);
		out.flush();
		return bitcode;
	}
} // namespace Scripting
