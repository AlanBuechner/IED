#pragma once
#include "Backend/HIR/TypeOperations.h"
#include "Backend/Lowering/ScriptLowering.h"
#include "Backend/Reflection/ReflectionScope.h"
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Scripting
{
	class ProgramModel;

	/// The object types reflection registers (docs/runtime/02 section 2; those in its scope, D-89), each with its flags (`reflect_TYPE_*`) and the
	/// thunks that construct, destroy and copy one: planned for the types the model has made, in its order, as lowering
	/// goes on making more.
	class TypeThunkPlan
	{
	public:
		/// A type's thunk to lower: `thunk` is its record.
		struct Job
		{
			const ObjectType* type = nullptr;
			TypeOperation op = TypeOperation::Construct;
			FunctionRecord* thunk = nullptr;
		};

		/// A registered type's flags and thunks; a thunk is null where the operation runs nothing or does not lower.
		struct Entry
		{
			uint32 flags = 0;
			const FunctionRecord* construct = nullptr;
			const FunctionRecord* destroy = nullptr;
			const FunctionRecord* copy = nullptr;
		};

		/// A script's plan leaves out the types its host registers.
		void ForScript(const ScriptLowering* script) { m_Script = script; }

		/// The thunks of the types made since the last plan, their records added to `records`.
		std::vector<Job> PlanNew(ProgramModel& model, std::deque<FunctionRecord>& records);
		/// The job's thunk lowered completely: the type has it.
		void Lowered(const Job& job);
		/// The job's thunk did not lower: the type cannot do what needs it (no start without its construct, no copy
		/// without its copy, neither without its destroy).
		void Failed(const Job& job);
		/// The types made since the last plan (by the reflection image's own questions) get their flags, without the
		/// operations that need code.
		void SettleLater(ProgramModel& model);

		const std::unordered_map<const ObjectType*, Entry>& Entries() const { return m_Entries; }

	private:
		bool Registers(const ProgramModel& model, const ObjectType* type) const;
		Job JobFor(ProgramModel& model, const ObjectType* type, TypeOperation op, std::deque<FunctionRecord>& records);
		void Without(const ObjectType* type, TypeOperation op);

		const ScriptLowering* m_Script = nullptr;
		mutable std::optional<ReflectionScope> m_Scope; // made on the first question, from the model
		std::unordered_map<const ObjectType*, Entry> m_Entries;
		uint32 m_TypesPlanned = 0; // the model's object types looked at so far
	};
} // namespace Scripting
