#include "Backend/ProgramBackend.h"
#include <algorithm>

namespace Scripting
{
	void ProgramBackend::SplitInto(unsigned count, PartitionCopies copies)
	{
		m_Partitions = std::max(count, 1u);
		m_Emitter.SplitInto(m_Partitions);
		m_Emit.copies = copies;
	}

	bool ProgramBackend::Lower(ProgramModel& model, const Core::String& entryName, std::string* errorMessage, const TestHarnessOptions* tests)
	{
		m_Emit.entryName = entryName;
		m_Emit.tests = tests;
		m_Emit.script = m_Lowering.script ? &*m_Lowering.script : nullptr;
		if (!LowerProgram(model, errorMessage)) return false;
		const bool emitted = m_Emitter.Emit(model, m_Lowered->Program(), m_Emit, m_Lowered->Pool(), errorMessage);
		m_Errors.Append(m_Emitter.Errors());
		m_StepSeconds.insert(m_StepSeconds.end(), m_Emitter.StepSeconds().begin(), m_Emitter.StepSeconds().end());
		if (!emitted && m_Emitter.Errors().HasErrors()) Failed(errorMessage);
		return emitted;
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
		return m_Emitter.Reflection().BuildImageModule(context);
	}
} // namespace Scripting
