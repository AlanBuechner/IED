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
#include <llvm/TargetParser/Triple.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <llvm/ExecutionEngine/JITLink/JITLink.h>
#include <llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h>
#endif

namespace Scripting
{
	namespace
	{
#if !defined(_WIN32)
		// A script loaded anywhere in the address space reaches what it does not define - the host's code and data, a
		// layer's - through the GOT and stubs JITLink builds: no declaration is dso_local (nor hidden, which implies it),
		// so the code never names one by a 32-bit PC-relative offset, which cannot span that distance (D-86).
		void ReachDeclarationsIndirectly(llvm::Module& module)
		{
			for (llvm::GlobalValue& value : module.global_values())
			{
				if (!value.isDeclaration()) continue;
				if (auto* function = llvm::dyn_cast<llvm::Function>(&value); function && function->isIntrinsic()) continue;
				value.setVisibility(llvm::GlobalValue::DefaultVisibility);
				value.setDSOLocal(false);
			}
		}

		// A debug script's functions where JITLink placed them, to name a fault's place by.
		struct ScriptFunction
		{
			uint64 start = 0;
			uint64 size = 0;
			std::string name;
		};
		using ScriptFunctions = std::vector<ScriptFunction>;

		// Where JITLink placed the script's blocks, once they have their addresses: its image, against which a fault's
		// place is told (script+0x...), as the memory manager's block is on Windows; and a debug script's functions.
		class ImageBounds : public llvm::orc::LinkGraphLinkingLayer::Plugin
		{
		public:
			ImageBounds(std::shared_ptr<ScriptImage> image, std::shared_ptr<ScriptFunctions> functions)
				: m_Image(std::move(image)), m_Functions(std::move(functions)) {}

			void modifyPassConfig(llvm::orc::MaterializationResponsibility&, llvm::jitlink::LinkGraph&, llvm::jitlink::PassConfiguration& config) override
			{
				config.PostAllocationPasses.push_back([image = m_Image, functions = m_Functions](llvm::jitlink::LinkGraph& graph) -> llvm::Error {
					for (llvm::jitlink::Block* block : graph.blocks())
					{
						const uintptr_t low = static_cast<uintptr_t>(block->getAddress().getValue());
						const uintptr_t high = low + static_cast<uintptr_t>(block->getSize());
						if (image->low == 0 || low < image->low) image->low = low;
						if (high > image->high) image->high = high;
					}
					if (functions)
						for (llvm::jitlink::Symbol* symbol : graph.defined_symbols())
							if (symbol->isCallable() && symbol->hasName() && symbol->getSize() > 0)
								functions->push_back({ symbol->getAddress().getValue(), symbol->getSize(), (*symbol->getName()).str() });
					return llvm::Error::success();
				});
			}
			llvm::Error notifyFailed(llvm::orc::MaterializationResponsibility&) override { return llvm::Error::success(); }
			llvm::Error notifyRemovingResources(llvm::orc::JITDylib&, llvm::orc::ResourceKey) override { return llvm::Error::success(); }
			void notifyTransferringResources(llvm::orc::JITDylib&, llvm::orc::ResourceKey, llvm::orc::ResourceKey) override {}

		private:
			std::shared_ptr<ScriptImage> m_Image;
			std::shared_ptr<ScriptFunctions> m_Functions;
		};
#endif

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

			HostSymbolTable m_Host; // its own copy: the layers it names stay as long as this script's JIT
		};

#if defined(_WIN32)
		// One block per object, code first, within reach of its data (the image-relative tables need it). Its .pdata
		// is registered against the block's lowest address, the base RuntimeDyld resolves image-relative relocations
		// against; the block is the script's image for its throws (C4Runtime/Script/ScriptThrowWin32.c).
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
#endif

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
#if defined(_WIN32)
			AddPersonalityThunk(*code.module);
#else
			ReachDeclarationsIndirectly(*code.module);
#endif
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
#if !defined(_WIN32)
			// JITLink places a script anywhere in the address space: position-independent code reaches the host's code
			// and data through the GOT and stubs it builds, never by a 32-bit absolute address (D-86).
			builder.setRelocationModel(llvm::Reloc::PIC_);
			builder.setCodeModel(llvm::CodeModel::Small);
#endif
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

#if !defined(_WIN32)
			// A debug script's place by its function, as its DLL's PDB tells it on Windows.
			std::string Describe(uint64 address) const override
			{
				if (!m_Functions) return "";
				for (const ScriptFunction& function : *m_Functions)
					if (address >= function.start && address < function.start + function.size)
					{
						char offset[24];
						std::snprintf(offset, sizeof(offset), "+0x%llx", static_cast<unsigned long long>(address - function.start));
						return m_Name + "!" + function.name + offset;
					}
				return "";
			}

			void Name(std::string name, std::shared_ptr<ScriptFunctions> functions)
			{
				m_Name = std::move(name);
				m_Functions = std::move(functions);
			}
#endif

		private:
			std::unique_ptr<llvm::orc::LLJIT> m_Jit;
			std::shared_ptr<ScriptImage> m_Image;
#if !defined(_WIN32)
			std::string m_Name;
			std::shared_ptr<ScriptFunctions> m_Functions;
#endif
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
		auto addresses = std::make_shared<std::unordered_map<std::string, void*>>();
		for (size_t i = 0; i < host.symbols.size(); ++i) (*addresses)[host.symbols[i]] = image->entries[i];
		HostSymbolTable symbols;
		symbols.m_Addresses = std::move(addresses);
		return symbols;
	}

	void* HostSymbolTable::Find(const std::string& name) const
	{
		auto it = m_Addresses->find(name);
		if (it != m_Addresses->end()) return it->second;
		for (auto layer = m_Layers.rbegin(); layer != m_Layers.rend(); ++layer)
			if ((*layer)->names.count(name)) return (*layer)->script->Find(name);
		return nullptr;
	}

	HostSymbolTable HostSymbolTable::Extended(std::shared_ptr<const LinkedScript> script, const std::vector<std::string>& names) const
	{
		auto layer = std::make_shared<Layer>();
		layer->script = std::move(script);
		layer->names.insert(names.begin(), names.end());
		HostSymbolTable extended = *this;
		extended.m_Layers.push_back(std::move(layer));
		return extended;
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
		KeptObject kept(generated);
		llvm::orc::LLJITBuilder builder;
		builder.setJITTargetMachineBuilder(MachineBuilder(target, code.optimize))
			.setCompileFunctionCreator([&kept](llvm::orc::JITTargetMachineBuilder builder) -> llvm::Expected<std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler>> {
				auto machine = builder.createTargetMachine();
				if (!machine) return machine.takeError();
				return std::make_unique<llvm::orc::TMOwningSimpleCompiler>(std::move(*machine), &kept);
			});
#if defined(_WIN32)
		const ImageFn add = host.Function<ImageFn>("c4_script_image_add");
		const ImageRemoveFn remove = host.Function<ImageRemoveFn>("c4_script_image_remove");
		builder.setObjectLinkingLayerCreator([&](llvm::orc::ExecutionSession& session) -> llvm::Expected<std::unique_ptr<llvm::orc::ObjectLayer>> {
			auto layer = std::make_unique<llvm::orc::RTDyldObjectLinkingLayer>(session, [image, add, remove](const llvm::MemoryBuffer&) {
				return std::make_unique<ScriptMemoryManager>(image, add, remove);
			});
			layer->setOverrideObjectFlagsWithResponsibilityFlags(true); // COFF objects mark too little as exported
			layer->setAutoClaimResponsibilityForObjectSymbols(true);
			return std::move(layer);
		});
#endif
		// On Linux ORC's own linker for ELF (JITLink) loads the object and registers its .eh_frame, so a script's
		// exceptions unwind through its frames with the host's personality (C4Runtime/ExceptionsPosix.c).
		auto jit = builder.create();
		if (!jit) { Fail(error, "the JIT does not start: " + llvm::toString(jit.takeError())); return nullptr; }
#if !defined(_WIN32)
		const std::string debugName = code.debugName;
		auto functions = debugName.empty() ? nullptr : std::make_shared<ScriptFunctions>();
		if (auto* linking = llvm::dyn_cast<llvm::orc::ObjectLinkingLayer>(&(*jit)->getObjLinkingLayer()))
			linking->addPlugin(std::make_shared<ImageBounds>(image, functions));
#endif
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
		auto linked = std::make_unique<JitScript>(std::move(*jit), image);
#if !defined(_WIN32)
		if (functions) linked->Name(debugName, functions);
#endif
		return linked;
	}
} // namespace Scripting
