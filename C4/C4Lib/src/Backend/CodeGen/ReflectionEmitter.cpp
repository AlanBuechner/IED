#include "Backend/CodeGen/ReflectionEmitter.h"
#include "Backend/Reflection/ReflectionScope.h"
#include "Backend/Reflection/ReflectionTables.h"
#include "Backend/Reflection/ReflectsCheck.h"
#include "Model/ProgramModel.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

namespace C4Compiler
{
	namespace
	{
		/// Reflection registers before every static constructor, so the program's global starts can use it.
		constexpr int kReflectionPriority = 65534;

		bool IsThunk(UnitKind kind) { return kind == UnitKind::InvokeThunk || kind == UnitKind::TypeThunk; }
	}

	// Each partition finishes its own thunks at once (each has its own module); the functions are kept by record.
	void ReflectionEmitter::FinishThunks(const LoweredProgram& program, WorkerPool& pool)
	{
		std::vector<const LoweredUnit*> thunks;
		for (const LoweredUnit& unit : program.units)
			if (IsThunk(unit.kind)) thunks.push_back(&unit);
		std::vector<llvm::Function*> finished(thunks.size());
		pool.Run(m_Partitions.Count(), [&](size_t partition) {
			for (size_t i = 0; i < thunks.size(); ++i)
				if (m_Partitions.Of(*thunks[i]->placedBy) == partition) finished[i] = FinishThunk(*thunks[i]->tree->record, static_cast<unsigned>(partition));
		});
		for (size_t i = 0; i < thunks.size(); ++i) m_ThunkFunctions[thunks[i]->tree->record] = finished[i];
	}

	// Only the reflection image names a thunk: a local of the program, which a split program's image module reaches.
	// Reflection calls through it seldom: small, with its target called rather than copied into it.
	llvm::Function* ReflectionEmitter::FinishThunk(const FunctionRecord& thunk, unsigned partition)
	{
		ModuleContext& home = m_Partitions.Partition(partition);
		llvm::Function* emitted = home.GetOrCreateFunction(thunk);
		emitted->setName(home.LocalName(thunk.mangledName.c_str()));
		home.MakeLocal(*emitted);
		emitted->addFnAttr(llvm::Attribute::MinSize);
		emitted->addFnAttr(llvm::Attribute::OptimizeForSize);
		return emitted;
	}

	bool ReflectionEmitter::Emit(ProgramModel& model, LoweredProgram& program, const Core::String& tag, ErrorTracker& errors, WorkerPool& pool)
	{
		const HostEntities host{ m_Script ? m_Script->hostSymbols : nullptr, m_Script ? m_Script->hostTypes : nullptr,
			m_Script ? m_Script->interfaceUnits : 0 };
		const InvokeThunks invokes = InvokeEntries(program, pool);
		const TypeThunks types = TypeEntries(model, program);
		ImageDescription description = DescribeReflectionImage(m_Partitions.Partition(0), model, invokes, types, host, errors);
		CheckReflectedArguments(model, program, ReflectionScope(model), errors);
		if (errors.HasErrors()) return false;
		if (description.Empty()) return true;
		llvm::GlobalVariable* image = m_Partitions.Split() ? KeepImageApart(std::move(description)) : BuildImageHere(description);
		if (!m_Script) RegisterImage(image, tag); // a script's loader registers its image under the script's tag
		return true;
	}

	InvokeThunks ReflectionEmitter::InvokeEntries(const LoweredProgram& program, WorkerPool& pool)
	{
		const std::vector<InvokeThunkUse>& uses = program.invokeThunks;
		std::vector<InvokeEntry> entries(uses.size());
		pool.Run(m_Partitions.Count(), [&](size_t partition) {
			for (size_t i = 0; i < uses.size(); ++i)
				if (m_Partitions.Of(*uses[i].target) == partition) entries[i] = InvokeEntryOf(uses[i]);
		});
		InvokeThunks thunks;
		for (size_t i = 0; i < uses.size(); ++i) thunks[uses[i].target] = entries[i];
		return thunks;
	}

	// The image names a target by its function in the partition that defines it, which declares nothing new there; a
	// function without code (a pure virtual method) records none.
	InvokeEntry ReflectionEmitter::InvokeEntryOf(const InvokeThunkUse& use)
	{
		ModuleContext& home = m_Partitions.Partition(m_Partitions.Of(*use.target));
		llvm::Function* code = HasCode(*use.target) ? home.GetOrCreateFunction(*use.target) : nullptr;
		llvm::Function* thunk = m_ThunkFunctions.at(use.thunk);
		return use.shared ? InvokeEntry{ thunk, home.GetOrCreateFunction(*use.target), code } : InvokeEntry{ thunk, nullptr, code };
	}

	// A type the reflection image's own questions made after lowering gets its flags, without the operations that need code.
	TypeThunks ReflectionEmitter::TypeEntries(ProgramModel& model, LoweredProgram& program) const
	{
		program.types.SettleLater(model);
		TypeThunks types;
		for (const auto& [type, entry] : program.types.Entries())
			types[type] = TypeEntry{ entry.flags, ThunkFunction(entry.construct), ThunkFunction(entry.destroy), ThunkFunction(entry.copy) };
		return types;
	}

	llvm::Function* ReflectionEmitter::ThunkFunction(const FunctionRecord* thunk) const
	{
		return thunk ? m_ThunkFunctions.at(thunk) : nullptr;
	}

	llvm::GlobalVariable* ReflectionEmitter::BuildImageHere(const ImageDescription& image)
	{
		ModuleContext& program = m_Partitions.Partition(0);
		return ReflectionTables(program.module(), [&program](const std::string& text) -> llvm::Constant* {
			return program.StringGlobal(ConstantValue::String(text));
		}).Build(image);
	}

	// Building the constants reads no model, so the output builds them where it assembles the image's object, and only
	// when no object of the image's key is kept (D-82).
	llvm::GlobalVariable* ReflectionEmitter::KeepImageApart(ImageDescription image)
	{
		llvm::Module& program = m_Partitions.Partition(0).module();
		m_ImageTriple = program.getTargetTriple().str();
		m_ImageLayout = program.getDataLayoutStr();
		m_Image = std::move(image);
		// Hashing the image's values (about 17 MB of them in the IED server) goes on beside the imports and the GC roots.
		m_ImageKeying = std::async(std::launch::async, [this] { return "reflection-" + m_Image.Key(m_ImageTriple + " " + m_ImageLayout); });
		return new llvm::GlobalVariable(program, llvm::Type::getInt8Ty(program.getContext()), true, llvm::GlobalValue::ExternalLinkage, nullptr,
			"__c4_reflect_image");
	}

	void ReflectionEmitter::RegisterImage(llvm::GlobalVariable* image, const Core::String& tag)
	{
		ModuleContext& program = m_Partitions.Partition(0);
		llvm::LLVMContext& context = program.context();
		auto* ptrTy = llvm::PointerType::get(context, 0);
		llvm::Function* registerImage = program.RuntimeFunction("c4_reflect_register_image", llvm::Type::getVoidTy(context), { ptrTy, ptrTy });
		auto* init = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(context), false), llvm::Function::InternalLinkage,
			"__c4_reflect_init", program.module());
		llvm::IRBuilder<> B(llvm::BasicBlock::Create(context, "entry", init));
		B.CreateCall(registerImage, { program.StringGlobal(ConstantValue::String(tag.c_str())), image });
		B.CreateRetVoid();
		llvm::appendToGlobalCtors(program.module(), init, kReflectionPriority);
	}

	const std::string& ReflectionEmitter::ImageKey()
	{
		if (m_ImageKeying.valid()) m_ImageKey = m_ImageKeying.get();
		return m_ImageKey;
	}

	std::unique_ptr<llvm::Module> ReflectionEmitter::BuildImageModule(llvm::LLVMContext& context) const
	{
		auto module = std::make_unique<llvm::Module>("c4_reflection", context);
		module->setTargetTriple(llvm::Triple(m_ImageTriple));
		module->setDataLayout(m_ImageLayout);
		ReflectionTables(*module).Build(m_Image);
		return module;
	}
} // namespace C4Compiler
