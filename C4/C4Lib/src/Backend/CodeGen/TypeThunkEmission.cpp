// CodeGenDriver's type-operation thunks (docs/runtime/02 section 2): what reflection runs to construct, destroy and
// copy an object of a type it knows only by its record. Each is emitted like an invoke thunk: lowered on the threads,
// placed by its name, local to the program and named only by the reflection image.

#include "Backend/CodeGen/CodeGenDriver.h"
#include "Model/Types/QualifiedName.h"
#include "ReflectABI.h"

namespace Scripting
{
	namespace
	{
		llvm::Function*& ThunkOf(TypeEntry& entry, TypeOperation op)
		{
			switch (op)
			{
			case TypeOperation::Construct: return entry.construct;
			case TypeOperation::Destroy: return entry.destroy;
			case TypeOperation::Copy: return entry.copy;
			}
			return entry.construct;
		}
	}

	bool CodeGenDriver::EmitTypeThunks(ProgramModel& model, std::string* errorMessage)
	{
		const std::vector<TypeThunkJob> jobs = PlanTypeThunks(model);
		if (jobs.empty()) return true;
		std::vector<LoweredBody> bodies = m_Lowering.LowerEach(model, jobs.size(), [&](size_t i, ErrorTracker& errors) {
			return LowerTypeOperationThunk(model, errors, *jobs[i].type, jobs[i].op, *jobs[i].thunk);
		});
		std::vector<Placed> lowered;
		std::vector<const TypeThunkJob*> kept;
		for (size_t i = 0; i < jobs.size(); ++i)
		{
			if (!bodies[i].tree->complete)
			{
				WithoutThunk(jobs[i].type, jobs[i].op);
				continue;
			}
			lowered.push_back({ bodies[i].tree.get(), PartitionOf(*jobs[i].thunk) });
			kept.push_back(&jobs[i]);
			m_Trees.push_back(std::move(bodies[i].tree));
		}
		Core::Vector<const FunctionRecord*> batch;
		EmitPlaced(model, lowered, batch);
		FinishThunks(lowered);
		for (const TypeThunkJob* job : kept) ThunkOf(m_TypeThunks[job->type], job->op) = m_ThunkFunctions.at(job->thunk);
		return EmitReachable(model, batch, errorMessage);
	}

	// The model's list of types may grow while a type is asked about (a template instance its parts need): each is
	// read by its index, the list's length again each turn.
	std::vector<CodeGenDriver::TypeThunkJob> CodeGenDriver::PlanTypeThunks(ProgramModel& model)
	{
		std::vector<TypeThunkJob> jobs;
		const Core::Vector<const ObjectType*>& types = model.ObjectTypes();
		for (; m_TypesPlanned < types.Count(); ++m_TypesPlanned)
		{
			const ObjectType* type = types[m_TypesPlanned];
			if (!RegistersType(model, type)) continue;
			const TypeOperations ops = TypeOperationsOf(model, type);
			m_TypeThunks[type].flags = ops.flags;
			if (ops.construct) jobs.push_back(TypeThunk(model, type, TypeOperation::Construct));
			if (ops.destroy) jobs.push_back(TypeThunk(model, type, TypeOperation::Destroy));
			if (ops.copy) jobs.push_back(TypeThunk(model, type, TypeOperation::Copy));
		}
		return jobs;
	}

	CodeGenDriver::TypeThunkJob CodeGenDriver::TypeThunk(ProgramModel& model, const ObjectType* type, TypeOperation op)
	{
		FunctionRecord& thunk = m_ThunkRecords.emplace_back();
		thunk.type = TypeOperationThunkType(model, op);
		thunk.mangledName = TypeOperationSymbol(type, op).c_str();
		return { type, op, &thunk };
	}

	void CodeGenDriver::WithoutThunk(const ObjectType* type, TypeOperation op)
	{
		uint32& flags = m_TypeThunks[type].flags;
		if (op != TypeOperation::Copy) flags &= ~reflect_TYPE_DEFAULT_CONSTRUCTIBLE;
		if (op != TypeOperation::Construct) flags &= ~reflect_TYPE_COPYABLE;
	}

	void CodeGenDriver::SettleTypeEntries(ProgramModel& model)
	{
		const Core::Vector<const ObjectType*>& types = model.ObjectTypes();
		for (; m_TypesPlanned < types.Count(); ++m_TypesPlanned)
		{
			const ObjectType* type = types[m_TypesPlanned];
			if (!RegistersType(model, type)) continue;
			const TypeOperations ops = TypeOperationsOf(model, type);
			m_TypeThunks[type].flags = ops.flags;
			if (ops.construct) WithoutThunk(type, TypeOperation::Construct);
			if (ops.destroy) WithoutThunk(type, TypeOperation::Destroy);
			if (ops.copy) WithoutThunk(type, TypeOperation::Copy);
		}
	}

	// A template's own definition is no type of its own; a script's host registers its own types.
	bool CodeGenDriver::RegistersType(const ProgramModel& model, const ObjectType* type) const
	{
		if (!ReflectedType(model, type) || type->GetTemplateParameterCount() != 0) return false;
		return !m_Script || !m_Script->hostTypes || !m_Script->hostTypes->count(QualifiedName(type).c_str());
	}
} // namespace Scripting
