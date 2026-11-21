#include "Backend/Lowering/TypeThunkPlan.h"
#include "Model/ProgramModel.h"
#include "Model/ReflectedEntities.h"
#include "ReflectABI.h"

namespace Scripting
{
	namespace
	{
		const FunctionRecord*& ThunkOf(TypeThunkPlan::Entry& entry, TypeOperation op)
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

	// The model's list of types may grow while a type is asked about (a template instance its parts need): each is read
	// by its index, the list's length again each turn.
	std::vector<TypeThunkPlan::Job> TypeThunkPlan::PlanNew(ProgramModel& model, std::deque<FunctionRecord>& records)
	{
		std::vector<Job> jobs;
		const Core::Vector<const ObjectType*>& types = model.ObjectTypes();
		for (; m_TypesPlanned < types.Count(); ++m_TypesPlanned)
		{
			const ObjectType* type = types[m_TypesPlanned];
			if (!Registers(model, type)) continue;
			const TypeOperations ops = TypeOperationsOf(model, type);
			m_Entries[type].flags = ops.flags;
			if (ops.construct) jobs.push_back(JobFor(model, type, TypeOperation::Construct, records));
			if (ops.destroy) jobs.push_back(JobFor(model, type, TypeOperation::Destroy, records));
			if (ops.copy) jobs.push_back(JobFor(model, type, TypeOperation::Copy, records));
		}
		return jobs;
	}

	void TypeThunkPlan::Lowered(const Job& job)
	{
		ThunkOf(m_Entries[job.type], job.op) = job.thunk;
	}

	void TypeThunkPlan::Failed(const Job& job)
	{
		Without(job.type, job.op);
	}

	void TypeThunkPlan::SettleLater(ProgramModel& model)
	{
		const Core::Vector<const ObjectType*>& types = model.ObjectTypes();
		for (; m_TypesPlanned < types.Count(); ++m_TypesPlanned)
		{
			const ObjectType* type = types[m_TypesPlanned];
			if (!Registers(model, type)) continue;
			const TypeOperations ops = TypeOperationsOf(model, type);
			m_Entries[type].flags = ops.flags;
			if (ops.construct) Without(type, TypeOperation::Construct);
			if (ops.destroy) Without(type, TypeOperation::Destroy);
			if (ops.copy) Without(type, TypeOperation::Copy);
		}
	}

	// A template's own definition is no type of its own; a script's host registers its own types.
	bool TypeThunkPlan::Registers(const ProgramModel& model, const ObjectType* type) const
	{
		if (!ReflectedType(model, type) || type->GetTemplateParameterCount() != 0) return false;
		return !m_Script || !m_Script->HostRegisters(*type);
	}

	TypeThunkPlan::Job TypeThunkPlan::JobFor(ProgramModel& model, const ObjectType* type, TypeOperation op, std::deque<FunctionRecord>& records)
	{
		FunctionRecord& thunk = records.emplace_back();
		thunk.type = TypeOperationThunkType(model, op);
		thunk.mangledName = TypeOperationSymbol(type, op).c_str();
		return { type, op, &thunk };
	}

	void TypeThunkPlan::Without(const ObjectType* type, TypeOperation op)
	{
		uint32& flags = m_Entries[type].flags;
		if (op != TypeOperation::Copy) flags &= ~reflect_TYPE_DEFAULT_CONSTRUCTIBLE;
		if (op != TypeOperation::Construct) flags &= ~reflect_TYPE_COPYABLE;
	}
} // namespace Scripting
