#include "Script/ScriptJit.h"
#include "Script/ScriptModulePasses.h"
#include "Script/ScriptSymbols.h"
#include <llvm/TargetParser/Host.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/ObjectCache.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Scripting
{
	namespace
	{
		using ImageFn = void (*)(const void*, const void*);
		using ImageRemoveFn = void (*)(const void*);

		// Resolves what a script names (ResolveScriptSymbol). The host's personality routine goes by another name: the
		// image has its own (AddPersonalityThunk).
		class HostGenerator : public llvm::orc::DefinitionGenerator
		{
		public:
			explicit HostGenerator(const HostSymbolTable& host) : m_Host(host) {}

			llvm::Error tryToGenerate(llvm::orc::LookupState&, llvm::orc::LookupKind, llvm::orc::JITDylib& library, llvm::orc::JITDylibLookupFlags,
				const llvm::orc::SymbolLookupSet& symbols) override
			{
				llvm::orc::SymbolMap found;
				for (const auto& [name, flags] : symbols)
					if (void* address = Resolve((*name).str())) found[name] = { llvm::orc::ExecutorAddr::fromPtr(address), llvm::JITSymbolFlags::Exported };
				return found.empty() ? llvm::Error::success() : library.define(llvm::orc::absoluteSymbols(std::move(found)));
			}

		private:
			void* Resolve(const std::string& name) const { return ResolveScriptSymbol(m_Host, name); }

			const HostSymbolTable& m_Host;
		};

		// One block per object, code first, within reach of its data (the image-relative tables need it). Its .pdata
		// is registered against the block's lowest address, the base RuntimeDyld resolves image-relative relocations
		// against; the block is the script's image for its throws (C4Runtime/Script/ScriptThrowC.c).
		class ScriptMemoryManager : public llvm::SectionMemoryManager
		{
		public:
			ScriptMemoryManager(std::shared_ptr<ScriptImage> image, ImageFn add, ImageRemoveFn remove)
				: llvm::SectionMemoryManager(nullptr, true), m_Image(std::move(image)), m_Add(add), m_Remove(remove) {}

			~ScriptMemoryManager() override { Deregister(); }

			uint8_t* allocateCodeSection(uintptr_t size, unsigned align, unsigned id, llvm::StringRef name) override
			{
				return Track(llvm::SectionMemoryManager::allocateCodeSection(size, align, id, name), size);
			}

			uint8_t* allocateDataSection(uintptr_t size, unsigned align, unsigned id, llvm::StringRef name, bool readOnly) override
			{
				return Track(llvm::SectionMemoryManager::allocateDataSection(size, align, id, name, readOnly), size);
			}

			void registerEHFrames(uint8_t* address, uint64_t, size_t size) override
			{
				auto* table = reinterpret_cast<PRUNTIME_FUNCTION>(address);
				if (RtlAddFunctionTable(table, static_cast<DWORD>(size / sizeof(RUNTIME_FUNCTION)), static_cast<DWORD64>(m_Image->low)))
					m_Tables.push_back(table);
				if (!m_Registered && m_Add)
				{
					m_Add(reinterpret_cast<const void*>(m_Image->low), reinterpret_cast<const void*>(m_Image->high));
					m_Registered = true;
				}
			}

			void deregisterEHFrames() override { Deregister(); }

		private:
			uint8_t* Track(uint8_t* p, uintptr_t size)
			{
				if (!p) return p;
				const auto at = reinterpret_cast<uintptr_t>(p);
				m_Image->low = m_Image->low ? std::min(m_Image->low, at) : at;
				m_Image->high = std::max(m_Image->high, at + size);
				return p;
			}

			void Deregister()
			{
				for (PRUNTIME_FUNCTION table : m_Tables) RtlDeleteFunctionTable(table);
				m_Tables.clear();
				if (m_Registered && m_Remove) m_Remove(reinterpret_cast<const void*>(m_Image->low));
				m_Registered = false;
			}

			std::shared_ptr<ScriptImage> m_Image;
			ImageFn m_Add;
			ImageRemoveFn m_Remove;
			std::vector<PRUNTIME_FUNCTION> m_Tables;
			bool m_Registered = false;
		};

		// .xdata names the personality routine by an image-relative offset, so it must live in the image: a function of
		// that name that jumps to the host's through an absolute pointer.
		void AddPersonalityThunk(llvm::Module& module)
		{
			llvm::Function* personality = module.getFunction("__CxxFrameHandler3");
			if (!personality || !personality->isDeclaration()) return;
			llvm::LLVMContext& context = module.getContext();
			llvm::FunctionType* type = personality->getFunctionType();
			llvm::Function* host = llvm::Function::Create(type, llvm::Function::ExternalLinkage, "__c4_host_CxxFrameHandler3", module);
			auto* slot = new llvm::GlobalVariable(module, llvm::PointerType::get(context, 0), true, llvm::GlobalValue::InternalLinkage, host,
				"__c4_personality_slot");
			llvm::IRBuilder<> B(llvm::BasicBlock::Create(context, "entry", personality));
			std::vector<llvm::Value*> arguments;
			for (llvm::Argument& argument : personality->args()) arguments.push_back(&argument);
			llvm::CallInst* call = B.CreateCall(type, B.CreateLoad(llvm::PointerType::get(context, 0), slot), arguments);
			call->setTailCallKind(llvm::CallInst::TCK_MustTail);
			B.CreateRet(call);
		}

		// The asm parser too: a script's asm blocks, and a debug script's import thunks, are assembled with its code.
		void InitializeTarget()
		{
			static std::once_flag once;
			std::call_once(once, [] {
				llvm::InitializeNativeTarget();
				llvm::InitializeNativeTargetAsmPrinter();
				llvm::InitializeNativeTargetAsmParser();
			});
		}

		// Keeps the machine code the JIT generates from a module, for a script cache.
		class KeptObject : public llvm::ObjectCache
		{
		public:
			explicit KeptObject(std::string* into) : m_Into(into) {}
			void notifyObjectCompiled(const llvm::Module*, llvm::MemoryBufferRef object) override
			{
				if (m_Into) m_Into->assign(object.getBufferStart(), object.getBufferSize());
			}
			std::unique_ptr<llvm::MemoryBuffer> getObject(const llvm::Module*) override { return nullptr; }

		private:
			std::string* m_Into;
		};

		llvm::Error AddCode(llvm::orc::LLJIT& jit, ScriptCode code)
		{
			if (!code.module) return jit.addObjectFile(llvm::MemoryBuffer::getMemBufferCopy(code.object, "cached script"));
			AddPersonalityThunk(*code.module);
			return jit.addIRModule(llvm::orc::ThreadSafeModule(std::move(code.module), std::move(code.context)));
		}

		llvm::orc::JITTargetMachineBuilder MachineBuilder(const CpuTarget& target, bool optimize)
		{
			InitializeTarget();
			llvm::orc::JITTargetMachineBuilder builder{ llvm::Triple(llvm::sys::getProcessTriple()) };
			builder.setCodeGenOptLevel(optimize ? llvm::CodeGenOptLevel::Default : llvm::CodeGenOptLevel::None);
			builder.setCPU(target.Cpu());
			builder.getFeatures().AddFeature(target.LlvmFeatures()); // the whole `+a,-b` list, as clang passes it
			builder.getOptions().EmulatedTLS = true;
			return builder;
		}

		bool Fail(std::string* error, const std::string& why)
		{
			if (error) *error = why;
			return false;
		}

		// A script its own JIT linked in memory.
		class JitScript final : public LinkedScript
		{
		public:
			JitScript(std::unique_ptr<llvm::orc::LLJIT> jit, std::shared_ptr<ScriptImage> image) : m_Jit(std::move(jit)), m_Image(std::move(image)) {}

			void* Find(const std::string& name) const override
			{
				auto symbol = m_Jit->lookup(name);
				if (!symbol)
				{
					llvm::consumeError(symbol.takeError());
					return nullptr;
				}
				return symbol->toPtr<void*>();
			}

			const ScriptImage& Image() const override { return *m_Image; }

		private:
			std::unique_ptr<llvm::orc::LLJIT> m_Jit;
			std::shared_ptr<ScriptImage> m_Image;
		};
	}

	std::optional<HostSymbolTable> HostSymbolTable::Read(const HostInterface& host, const void* table, std::string* error)
	{
		struct Layout { const uint8* id; uint64 count; void* entries[1]; };
		const auto* image = static_cast<const Layout*>(table);
		if (!image)
		{
			Fail(error, "the program has no script host table: build it with --script-host");
			return std::nullopt;
		}
		if (std::memcmp(image->id, host.buildId.data(), host.buildId.size()) != 0)
		{
			Fail(error, "the interface is another build's: the program and its .c4i must come from the same build");
			return std::nullopt;
		}
		if (image->count != host.symbols.size())
		{
			Fail(error, "the program's symbol table has " + std::to_string(image->count) + " entries, its interface names " + std::to_string(host.symbols.size()));
			return std::nullopt;
		}
		HostSymbolTable symbols;
		for (size_t i = 0; i < host.symbols.size(); ++i) symbols.m_Addresses[host.symbols[i]] = image->entries[i];
		return symbols;
	}

	void* HostSymbolTable::Find(const std::string& name) const
	{
		auto it = m_Addresses.find(name);
		return it == m_Addresses.end() ? nullptr : it->second;
	}


	std::unique_ptr<llvm::TargetMachine> ScriptTargetMachine(const CpuTarget& target, std::string* error)
	{
		auto machine = MachineBuilder(target, true).createTargetMachine();
		if (!machine)
		{
			Fail(error, "no target machine: " + llvm::toString(machine.takeError()));
			return nullptr;
		}
		return std::move(*machine);
	}

	std::unique_ptr<llvm::TargetMachine> ScriptDllTargetMachine(const CpuTarget& target, bool optimize, std::string* error)
	{
		llvm::orc::JITTargetMachineBuilder builder = MachineBuilder(target, optimize);
		builder.setCodeModel(llvm::CodeModel::Small);
		builder.setRelocationModel(llvm::Reloc::PIC_); // every address relative to the code, as clang compiles for Windows
		auto machine = builder.createTargetMachine();
		if (!machine)
		{
			Fail(error, "no target machine: " + llvm::toString(machine.takeError()));
			return nullptr;
		}
		return std::move(*machine);
	}

	std::unique_ptr<LinkedScript> LinkScript(ScriptCode code, const HostSymbolTable& host, const CpuTarget& target, std::string* generated,
		std::string* error)
	{
		auto image = std::make_shared<ScriptImage>();
		const ImageFn add = host.Function<ImageFn>("c4_script_image_add");
		const ImageRemoveFn remove = host.Function<ImageRemoveFn>("c4_script_image_remove");
		KeptObject kept(generated);
		auto jit = llvm::orc::LLJITBuilder()
			.setJITTargetMachineBuilder(MachineBuilder(target, code.optimize))
			.setCompileFunctionCreator([&kept](llvm::orc::JITTargetMachineBuilder builder) -> llvm::Expected<std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler>> {
				auto machine = builder.createTargetMachine();
				if (!machine) return machine.takeError();
				return std::make_unique<llvm::orc::TMOwningSimpleCompiler>(std::move(*machine), &kept);
			})
			.setObjectLinkingLayerCreator([&](llvm::orc::ExecutionSession& session) -> llvm::Expected<std::unique_ptr<llvm::orc::ObjectLayer>> {
				auto layer = std::make_unique<llvm::orc::RTDyldObjectLinkingLayer>(session, [image, add, remove](const llvm::MemoryBuffer&) {
					return std::make_unique<ScriptMemoryManager>(image, add, remove);
				});
				layer->setOverrideObjectFlagsWithResponsibilityFlags(true); // COFF objects mark too little as exported
				layer->setAutoClaimResponsibilityForObjectSymbols(true);
				return std::move(layer);
			})
			.create();
		if (!jit) { Fail(error, "the JIT does not start: " + llvm::toString(jit.takeError())); return nullptr; }
		auto sessionErrors = std::make_shared<std::string>(); // ORC reports a failed resolution here, not only in the lookup's error
		(*jit)->getExecutionSession().setErrorReporter([sessionErrors](llvm::Error e) { *sessionErrors += llvm::toString(std::move(e)) + "\n"; });
		llvm::orc::JITDylib& library = (*jit)->getMainJITDylib();
		library.addGenerator(std::make_unique<HostGenerator>(host));

		if (auto added = AddCode(**jit, std::move(code)))
		{
			Fail(error, "the script does not link: " + llvm::toString(std::move(added)));
			return nullptr;
		}
		// Every script defines its module record: looking it up generates the whole module, resolving every name it uses.
		auto record = (*jit)->lookup(kScriptModuleSymbol);
		if (!record)
		{
			const std::string why = llvm::toString(record.takeError());
			Fail(error, "the script does not link: " + (sessionErrors->empty() ? why : *sessionErrors));
			return nullptr;
		}
		return std::make_unique<JitScript>(std::move(*jit), image);
	}
} // namespace Scripting
