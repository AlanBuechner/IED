#pragma once
#include "Backend/MIR/MirBody.h"
#include "Model/QueryResult.h"
#include <functional>
#include <vector>

namespace Scripting
{
	/// How far compile-time evaluation may go (roadmap 6's guards): it must end, so a call runs at most `steps`
	/// statements and terminators in all, at most `depth` calls deep.
	struct CtfeLimits
	{
		uint64 steps = 1000000;
		uint32 depth = 256;
	};

	/// A value while it is evaluated: its bytes, laid out as the target lays out its type (little-endian x64).
	using CtfeBytes = std::vector<uint8>;

	/// Where the interpreter finds a callee's lowered body (its HIR and MIR), or why that callee cannot run at compile time
	/// (it is not [compile_time], its body does not compile). The caller lowers and keeps the bodies.
	using CtfeBodies = std::function<QueryResult<const HIRFunction*>(const FunctionRecord&)>;

	/// Runs a function's MIR over folded arguments, at compile time (R6, docs/compiler/06-mir.md "Compile-time
	/// evaluation"): scalars and value types, every MIR terminator but an exception's, calls of other evaluable functions,
	/// fixed arrays. Memory is allocations of bytes (each slot of each frame one); a pointer is an allocation and an offset
	/// in it, and reading or writing past an allocation is an error, as is anything compile time cannot do (allocate on
	/// the heap, call through a pointer, run assembly, read a global that is not constant).
	class MirInterpreter
	{
	public:
		MirInterpreter(CtfeBodies bodies, CtfeLimits limits) : m_Bodies(std::move(bodies)), m_Limits(limits) {}

		/// `callee` run on `arguments` (each the bytes of its parameter): the bytes it returns, or why it cannot run.
		QueryResult<CtfeBytes> Call(const FunctionRecord& callee, const std::vector<CtfeBytes>& arguments, const SourceLocation& at);

		/// A scalar constant as the bytes of `type`, and back. Encode fails on a type that is no scalar.
		static bool Encode(const ConstantValue& value, const Type* type, CtfeBytes& out);
		static ConstantValue Decode(const CtfeBytes& bytes, const Type* type);

	private:
		struct Frame
		{
			const HIRFunction* fn = nullptr;
			std::vector<uint64> slots;   // each slot's storage (a pointer)
			uint64 self = 0;             // `this`
			CtfeBytes result;
			std::vector<std::pair<uint32, uint32>> regions; // the regions of the statements being run, innermost last
		};
		enum class Exit { Return, RegionEnd, Failed };

		// --- control (MirInterpreter.cpp) ---------------------------------------------------------------------
		bool Invoke(const FunctionRecord& callee, const HIRFunction& body, uint64 self, std::vector<CtfeBytes> arguments,
			const SourceLocation& at, CtfeBytes& result);
		Exit RunFrom(Frame& frame, MirBlockId start);
		bool RunStatement(Frame& frame, const MirStatement& statement);
		/// The block control goes to next, or c_MirNone when the frame returns or the region ends (`exit` says which).
		MirBlockId RunTerminator(Frame& frame, const MirBlock& block, Exit& exit);
		bool RunRegion(Frame& frame, const BlockNode* block);
		bool Step(const SourceLocation& at);

		// --- values (MirInterpreterValues.cpp) ----------------------------------------------------------------
		bool Eval(Frame& frame, const HIRNode* node, CtfeBytes& out);
		bool EvalCall(Frame& frame, const CallNode& call, CtfeBytes& out);
		bool EvalIntrinsic(Frame& frame, const IntrinsicOpNode& op, CtfeBytes& out);
		bool EvalConvert(Frame& frame, const ConvertNode& convert, CtfeBytes& out);
		bool EvalConstruct(Frame& frame, const ConstructNode& construct, CtfeBytes& out);
		bool EvalSequence(Frame& frame, const SequenceNode& sequence, CtfeBytes& out);
		bool Address(Frame& frame, const LValue* place, uint64& out);
		bool Load(Frame& frame, const LValue* place, const Type* type, CtfeBytes& out);
		bool Store(Frame& frame, const LValue* place, const CtfeBytes& value);
		bool Declare(Frame& frame, const LocalDeclNode& declaration);

		// --- memory -------------------------------------------------------------------------------------------
		uint64 Allocate(size_t bytes);
		bool Read(uint64 pointer, size_t size, CtfeBytes& out, const SourceLocation& at);
		bool Write(uint64 pointer, const CtfeBytes& value, const SourceLocation& at);
		bool Fail(const Core::String& message, const SourceLocation& at);

		CtfeBodies m_Bodies;
		CtfeLimits m_Limits;
		std::vector<CtfeBytes> m_Memory{ CtfeBytes() }; // allocation 0 is null's
		uint64 m_Steps = 0;
		uint32 m_Depth = 0;
		SourceLocation m_At;
		QueryResult<void> m_Failure = QueryResult<void>::Success();
	};
} // namespace Scripting
