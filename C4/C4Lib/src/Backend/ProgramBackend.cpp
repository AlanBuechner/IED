#include "Backend/ProgramBackend.h"
#include "Backend/CodeGen/ProgramPlacement.h"
#include <algorithm>

namespace C4Compiler
{
	void ProgramBackend::SplitInto(unsigned count, PartitionCopies copies)
	{
		m_Partitions = std::max(count, 1u);
		m_Emitter->SplitInto(m_Partitions);
		m_Emit.copies = copies;
	}

	bool ProgramBackend::Lower(ProgramModel& model, const Core::String& entryName, std::string* errorMessage, const TestHarnessOptions* tests)
	{
		m_Emit.entryName = entryName;
		m_Emit.tests = tests;
		m_Emit.script = m_Lowering.script ? &*m_Lowering.script : nullptr;
		if (!LowerProgram(model, errorMessage)) return false;
		if (!m_Serving) return Emit(model, errorMessage, nullptr);
		PartialEmission whole;
		whole.emitted.assign(m_Partitions, true);
		if (!Emit(model, errorMessage, &whole)) return false;
		Keep(whole);
		return true;
	}

	std::string ProgramBackend::Relower(const std::vector<const FunctionRecord*>& records)
	{
		if (!m_Lowered) return "the build before lowered nothing";
		if (std::string why = m_Lowered->Relower(records); !why.empty()) return why;
		m_Changed.insert(records.begin(), records.end());
		return {};
	}

	std::vector<const FunctionRecord*> ProgramBackend::LoweredFunctions() const
	{
		std::vector<const FunctionRecord*> lowered;
		if (!m_Lowered) return lowered;
		for (const LoweredUnit& unit : m_Lowered->Program().units)
			if (unit.kind == UnitKind::Function) lowered.push_back(unit.placedBy);
		return lowered;
	}

	// Each round emits from the start into modules of its own: a partition found stale joins the ones emitted, until none
	// left out is.
	bool ProgramBackend::EmitAgain(ProgramModel& model, std::unique_ptr<llvm::LLVMContext>& context, std::vector<RetiredEmission>& retired,
		std::string* errorMessage, const TestHarnessOptions* tests)
	{
		m_Emit.tests = tests;
		PartialEmission partial;
		partial.emitted = FirstEmitted(model);
		partial.facts = &m_Facts;
		partial.changed = m_Changed;
		do
		{
			retired.push_back({ std::move(context), std::move(m_Emitter) });
			context = std::make_unique<llvm::LLVMContext>();
			m_Emitter = std::make_unique<ProgramEmitter>(*context, m_System);
			m_Emitter->SplitInto(m_Partitions);
			if (!partial.Everything()) m_Emitter->Reflection().UseImage(m_Image);
			m_Errors.Clear();
			m_Errors.Append(m_Lowered->Errors());
			m_StepSeconds.clear();
			if (!Emit(model, errorMessage, m_Serving ? &partial : nullptr)) return false;
		} while (m_Serving && Widen(partial));
		if (m_Serving) Keep(partial);
		return true;
	}

	bool ProgramBackend::Emit(ProgramModel& model, std::string* errorMessage, PartialEmission* partial)
	{
		const bool emitted = m_Emitter->Emit(model, m_Lowered->Program(), m_Emit, m_Lowered->Pool(), errorMessage, partial);
		m_Emitter->Partitions().EmitOnly(nullptr); // `partial` goes with the caller
		m_Errors.Append(m_Emitter->Errors());
		m_StepSeconds.insert(m_StepSeconds.end(), m_Emitter->StepSeconds().begin(), m_Emitter->StepSeconds().end());
		if (!emitted && m_Emitter->Errors().HasErrors()) Failed(errorMessage);
		return emitted;
	}

	std::vector<bool> ProgramBackend::FirstEmitted(const ProgramModel& model) const
	{
		const bool kept = m_Serving && m_Partitions > 1 && m_Facts.size() == m_Partitions;
		std::vector<bool> emitted(m_Partitions, !kept);
		if (!kept) return emitted;
		const ProgramPlacement placement(model, m_Partitions);
		for (const FunctionRecord* record : m_Changed) emitted[placement.Of(*record)] = true;
		for (const SymbolInfo* global : m_ChangedStorage) emitted[placement.Of(*global)] = true;
		for (unsigned i = 1; i < m_Partitions; ++i)
			for (const FunctionRecord* copy : m_Facts[i].copies)
				if (m_Changed.count(copy)) emitted[i] = true;
		return emitted;
	}

	bool ProgramBackend::Widen(PartialEmission& partial)
	{
		if (partial.Everything()) return false;
		bool widened = false;
		for (size_t i = 0; i < partial.stale.size(); ++i)
			if (partial.stale[i]) widened = true;
		if (partial.stale[0]) partial.emitted.assign(partial.emitted.size(), true);
		else
			for (size_t i = 0; i < partial.stale.size(); ++i)
				if (partial.stale[i]) partial.emitted[i] = true;
		return widened;
	}

	void ProgramBackend::Keep(PartialEmission& partial)
	{
		m_Facts.resize(m_Partitions);
		for (unsigned i = 0; i < m_Partitions; ++i)
			if (partial.emitted[i]) m_Facts[i] = std::move(partial.made[i]);
		if (partial.Everything()) m_Image = m_Emitter->Reflection().KeepImage();
		m_Emitted = partial.emitted;
		m_Changed.clear();
		m_ChangedStorage.clear();
	}

	bool ProgramBackend::LowerProgram(ProgramModel& model, std::string* errorMessage)
	{
		m_Lowered = std::make_unique<ProgramLowering>(model, m_Lowering);
		const bool lowered = m_Lowered->Run();
		m_Errors.Append(m_Lowered->Errors());
		m_StepSeconds = m_Lowered->StepSeconds();
		return lowered || !Failed(errorMessage);
	}

	bool ProgramBackend::Failed(std::string* errorMessage) const
	{
		if (!m_Errors.HasErrors()) return false;
		std::string message;
		for (const CompilationError& error : m_Errors.GetErrors())
			if (error.severity != ErrorSeverity::Warning) message += "lowering: " + std::string(error.message.c_str()) + "\n";
		if (errorMessage) *errorMessage = message;
		return true;
	}

	const LoweringStats& ProgramBackend::LoweringStatistics() const
	{
		static const LoweringStats none;
		return m_Lowered ? m_Lowered->Stats() : none;
	}

	std::unique_ptr<llvm::Module> ProgramBackend::BuildReflectionModule(llvm::LLVMContext& context) const
	{
		return m_Emitter->Reflection().BuildImageModule(context);
	}
} // namespace C4Compiler
