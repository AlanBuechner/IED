#include "Compiler/PartitionMerge.h"
#include "Compiler/ModulePartitions.h"
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>

namespace C4Compiler
{
	namespace
	{
		// What the linker reports while the partitions are merged; the context's own handler is back after.
		class LinkErrors : public llvm::DiagnosticHandler
		{
		public:
			explicit LinkErrors(std::string& text) : m_Text(text) {}

			bool handleDiagnostics(const llvm::DiagnosticInfo& info) override
			{
				if (info.getSeverity() != llvm::DS_Error) return true;
				llvm::raw_string_ostream os(m_Text);
				llvm::DiagnosticPrinterRawOStream printer(os);
				if (!m_Text.empty()) os << "; ";
				info.print(printer);
				return true;
			}

		private:
			std::string& m_Text;
		};

		class CollectedLinkErrors
		{
		public:
			CollectedLinkErrors(llvm::LLVMContext& context, std::string& text) : m_Context(context), m_Previous(context.getDiagnosticHandler())
			{
				context.setDiagnosticHandler(std::make_unique<LinkErrors>(text));
			}
			~CollectedLinkErrors() { m_Context.setDiagnosticHandler(std::move(m_Previous)); }

		private:
			llvm::LLVMContext& m_Context;
			std::unique_ptr<llvm::DiagnosticHandler> m_Previous;
		};

		std::unique_ptr<llvm::Module> ReadPartition(const std::string& bitcode, llvm::LLVMContext& context, std::string& error)
		{
			llvm::Expected<std::unique_ptr<llvm::Module>> module = llvm::parseBitcodeFile(llvm::MemoryBufferRef(bitcode, "partition"), context);
			if (module) return std::move(*module);
			error = "cannot read a partition back to merge it: " + llvm::toString(module.takeError());
			return nullptr;
		}

		// A hidden `.local` definition is a local the emission made a global so another partition could name it.
		void MakeLocal(llvm::GlobalValue& value)
		{
			if (value.isDeclaration() || value.hasLocalLinkage() || ProgramName(value).size() == value.getName().size()) return;
			const std::string name = ProgramName(value).str();
			value.setLinkage(llvm::GlobalValue::InternalLinkage); // a local's visibility is the default one
			value.setName(name);
		}

		void MakeLocalsLocal(llvm::Module& module)
		{
			for (llvm::Function& function : module) MakeLocal(function);
			for (llvm::GlobalVariable& variable : module.globals()) MakeLocal(variable);
			for (llvm::GlobalAlias& alias : module.aliases()) MakeLocal(alias);
		}

		// `from` linked into `into`; false, with what the linker said in `error`, when it fails.
		bool Link(llvm::Module& into, std::unique_ptr<llvm::Module> from, std::string& error)
		{
			std::string said;
			bool failed = false;
			{
				CollectedLinkErrors collected(into.getContext(), said);
				failed = llvm::Linker::linkModules(into, std::move(from));
			}
			if (failed) error = "cannot merge the program's partitions: " + (said.empty() ? std::string("the linker failed") : said);
			return !failed;
		}
	}

	std::unique_ptr<llvm::Module> MergePartitions(llvm::LLVMContext& context, const std::vector<std::string>& partitions,
		const std::vector<KeyedModule::Builder>& beside, std::string& error)
	{
		std::unique_ptr<llvm::Module> merged;
		for (const std::string& bitcode : partitions)
		{
			std::unique_ptr<llvm::Module> partition = ReadPartition(bitcode, context, error);
			if (!partition) return nullptr;
			if (!merged) merged = std::move(partition);
			else if (!Link(*merged, std::move(partition), error)) return nullptr;
		}
		if (!merged)
		{
			error = "a program has at least one partition";
			return nullptr;
		}
		for (const KeyedModule::Builder& build : beside)
			if (!Link(*merged, build(context), error)) return nullptr;
		MakeLocalsLocal(*merged);
		return merged;
	}
}
