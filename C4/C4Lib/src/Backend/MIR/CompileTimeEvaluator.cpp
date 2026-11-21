#include "Backend/MIR/CompileTimeEvaluator.h"
#include "Backend/HIR/LoweringDriver.h"
#include "Backend/MIR/CompileTimeCheck.h"
#include "Compiler/ErrorTracker.h"
#include "Model/Builder/DeclarationQueries.h"
#include "Model/ProgramModel.h"
#include "Model/SymbolTable.h"
#include "Model/TypeHandle.h"
#include "Model/Types/FunctionType.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include <algorithm>
#include <string>

namespace Scripting
{
	namespace
	{
		Core::String NotMarked(const FunctionRecord& record)
		{
			return "'" + CompileTimeName(record) + "' is not [compile_time]: only a function marked [compile_time] runs at compile time";
		}

		bool IsPointer(const Type* type)
		{
			const Type* value = TypeHandle::UnwrapToValue(type);
			return value && (value->GetKind() == TypeKind::Pointer || value->GetKind() == TypeKind::FunPtr);
		}
	}

	const HIRFunction* CompileTimeEvaluator::Kept(const FunctionRecord& record)
	{
		const std::lock_guard<std::mutex> lock(m_Lock);
		const auto kept = m_Bodies.find(&record);
		return kept == m_Bodies.end() ? nullptr : kept->second.get();
	}

	QueryResult<std::unique_ptr<HIRFunction>> CompileTimeEvaluator::Lower(const FunctionRecord& record)
	{
		const std::thread::id thread = std::this_thread::get_id();
		{
			const std::lock_guard<std::mutex> lock(m_Lock);
			std::vector<const FunctionRecord*>& lowering = m_Lowering[thread];
			if (std::find(lowering.begin(), lowering.end(), &record) != lowering.end())
				return QueryResult<std::unique_ptr<HIRFunction>>::Fail(QueryError::CyclicDependency,
					"'" + CompileTimeName(record) + "' cannot run at compile time while it is compiled: a constant in it needs it");
			lowering.push_back(&record);
		}
		struct Done
		{
			CompileTimeEvaluator& self;
			std::thread::id thread;
			~Done()
			{
				const std::lock_guard<std::mutex> lock(self.m_Lock);
				self.m_Lowering[thread].pop_back();
			}
		} done{ *this, thread }; // also when lowering throws
		ErrorTracker errors; // its diagnostics are its build's too, where the program lowers it: here only the first says why
		auto body = std::make_unique<HIRFunction>(LowerFunction(m_Model, errors, &record));
		if (body->complete && body->mir) return QueryResult<std::unique_ptr<HIRFunction>>(std::move(body));
		Core::String why = "'" + CompileTimeName(record) + "' cannot run at compile time: it does not compile";
		for (const CompilationError& error : errors.GetErrors())
			if (error.severity != ErrorSeverity::Warning) { why += " (" + error.message + ")"; break; }
		return QueryResult<std::unique_ptr<HIRFunction>>::Fail(QueryError::NotAValue, why);
	}

	QueryResult<const HIRFunction*> CompileTimeEvaluator::BodyOf(const FunctionRecord& record)
	{
		if (!MayRunAtCompileTime(record)) return QueryResult<const HIRFunction*>::Fail(QueryError::NotAValue, NotMarked(record));
		const HIRFunction* body = Kept(record);
		if (!body)
		{
			// The types it takes and returns are whole before its body lowers (the body builds and reads them).
			if (auto signature = CompleteSignature(record); !signature.Ok())
				return QueryResult<const HIRFunction*>::Fail(signature.error, "'" + CompileTimeName(record) + "' cannot run at compile time yet: " + signature.message);
			CompleteWrittenTypes(record);
			// Only a body that compiled is kept: one that failed while the model resolved may compile once it has.
			QueryResult<std::unique_ptr<HIRFunction>> lowered = Lower(record);
			if (!lowered.Ok()) return QueryResult<const HIRFunction*>::Fail(lowered.error, lowered.message);
			const std::lock_guard<std::mutex> lock(m_Lock);
			body = m_Bodies.try_emplace(&record, std::move(lowered.value)).first->second.get(); // another thread's, when it was first
		}
		if (auto complete = CompleteTypes(*body); !complete.Ok())
			return QueryResult<const HIRFunction*>::Fail(complete.error, "'" + CompileTimeName(record) + "' cannot run at compile time yet: " + complete.message);
		return QueryResult<const HIRFunction*>(body);
	}

	QueryResult<void> CompileTimeEvaluator::CompleteSignature(const FunctionRecord& record)
	{
		DeclarationQueries* pending = m_Model.Pending();
		if (!pending || !record.type) return QueryResult<void>::Success();
		if (auto returned = pending->Complete(record.type->GetReturnType()); !returned.Ok()) return returned;
		for (const FunctionType::ParameterInfo& parameter : record.type->GetParameters())
			if (auto taken = pending->Complete(parameter.m_Type); !taken.Ok()) return taken;
		return QueryResult<void>::Success();
	}

	void CompileTimeEvaluator::CompleteWrittenTypes(const FunctionRecord& record)
	{
		DeclarationQueries* pending = m_Model.Pending();
		if (!pending || !record.bodyNode) return;
		std::vector<const AstNode*> open{ record.bodyNode };
		while (!open.empty())
		{
			const AstNode* node = open.back();
			open.pop_back();
			Ref<QualTypeNode> written;
			if (const auto* type = dynamic_cast<const QualTypeNode*>(node)) written = QualTypeNode::FromParts(type->BaseName(), type->FuncPtrType(), {}, false);
			else if (const auto* name = dynamic_cast<const NamespaceQualifiedNameNode*>(node))
				written = QualTypeNode::FromParts(NamespaceQualifiedNameNode::FromSegments(name->GetSegments()), Ref<AstNode>(), {}, false);
			// A name that is no type (a function, a variable, a template's parameter), or a type that cannot be whole yet (the
			// one being resolved, named through a pointer), is the body's lowering's to report if the body needs it whole.
			if (written && written->BaseName())
				if (auto resolved = m_Model.ResolveTypeInUnitContext(*written, record.LookupUnit(), record.scope); resolved.Ok() && resolved.value)
					(void)pending->Complete(resolved.value);
			for (const Ref<AstNode>& child : node->Children())
				if (child) open.push_back(child.Get());
		}
	}

	QueryResult<void> CompileTimeEvaluator::CompleteTypes(const HIRFunction& body)
	{
		DeclarationQueries* pending = m_Model.Pending();
		if (!pending) return QueryResult<void>::Success();
		for (const HIRFunction::Slot& slot : body.slots)
			if (auto held = pending->Complete(slot.type); !held.Ok()) return held;
		return QueryResult<void>::Success();
	}

	QueryResult<CtfeBytes> CompileTimeEvaluator::Run(const FunctionRecord& callee, const std::vector<ConstantValue>& arguments, const SourceLocation& at)
	{
		if (!IsCompileTime(callee)) return QueryResult<CtfeBytes>::Fail(QueryError::NotAValue, NotMarked(callee), at);
		const FunctionType* signature = callee.type;
		if (!signature || signature->GetParameterCount() != arguments.size())
			return QueryResult<CtfeBytes>::Fail(QueryError::NotCallable, "'" + CompileTimeName(callee) + "' does not take " +
				std::to_string(arguments.size()) + " arguments", at);
		std::vector<CtfeBytes> bytes(arguments.size());
		for (size_t i = 0; i < arguments.size(); ++i)
			if (!MirInterpreter::Encode(arguments[i], signature->GetParameters()[i].m_Type, bytes[i]))
				return QueryResult<CtfeBytes>::Fail(QueryError::NotAValue, "compile-time evaluation cannot pass a '" +
					signature->GetParameters()[i].m_Type->GetName() + "' to '" + CompileTimeName(callee) + "'", at);
		MirInterpreter interpreter([this](const FunctionRecord& record) { return BodyOf(record); }, m_Limits);
		return interpreter.Call(callee, bytes, at);
	}

	QueryResult<ConstantValue> CompileTimeEvaluator::Call(const FunctionRecord& callee, const std::vector<ConstantValue>& arguments, const SourceLocation& at)
	{
		const QueryResult<CtfeBytes> result = Run(callee, arguments, at);
		if (!result.Ok()) return QueryResult<ConstantValue>::Fail(result.error, result.message, result.loc);
		const Type* returned = callee.type->GetReturnType();
		const ConstantValue value = IsPointer(returned) ? ConstantValue() : MirInterpreter::Decode(result.value, returned);
		if (value.IsNone())
			return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "'" + CompileTimeName(callee) + "' returns a '" + returned->GetName() +
				"': a constant here is a number, a bool or a character", at);
		return QueryResult<ConstantValue>::Success(value);
	}

	QueryResult<std::vector<uint8>> CompileTimeEvaluator::CallForData(const FunctionRecord& callee, const std::vector<ConstantValue>& arguments, const SourceLocation& at)
	{
		return Run(callee, arguments, at);
	}
} // namespace Scripting
