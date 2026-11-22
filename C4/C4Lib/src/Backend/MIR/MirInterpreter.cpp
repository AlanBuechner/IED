// MirInterpreter's control: frames, blocks, statements, terminators and calls. Values and memory: MirInterpreterValues.cpp.
#include "Backend/MIR/MirInterpreter.h"
#include "Model/TypeHandle.h"
#include "Model/SymbolTable.h"

namespace C4Compiler
{
	namespace
	{
		Core::String NameOf(const FunctionRecord& record)
		{
			return record.name.empty() ? record.mangledName : record.name;
		}
	}

	QueryResult<CtfeBytes> MirInterpreter::Call(const FunctionRecord& callee, const std::vector<CtfeBytes>& arguments, const SourceLocation& at)
	{
		m_Failure = QueryResult<void>::Success();
		m_Steps = 0;
		m_Depth = 0;
		m_Memory.assign(1, CtfeBytes());
		const QueryResult<const HIRFunction*> body = m_Bodies(callee);
		if (!body.Ok()) return QueryResult<CtfeBytes>::Fail(body.error, body.message, body.loc.line ? body.loc : at);
		CtfeBytes result;
		if (!Invoke(callee, *body.value, 0, arguments, at, result))
			return QueryResult<CtfeBytes>::Fail(m_Failure.error, m_Failure.message, m_Failure.loc.line ? m_Failure.loc : at);
		return QueryResult<CtfeBytes>::Success(std::move(result));
	}

	bool MirInterpreter::Fail(const Core::String& message, const SourceLocation& at)
	{
		if (m_Failure.Ok()) m_Failure = QueryResult<void>::Fail(QueryError::NotAValue, message, at.line ? at : m_At);
		return false;
	}

	bool MirInterpreter::Step(const SourceLocation& at)
	{
		if (++m_Steps <= m_Limits.steps) return true;
		return Fail("compile-time evaluation ran more than " + Core::String(std::to_string(m_Limits.steps).c_str()) +
			" steps: a loop that does not end, or work too big to do while compiling", at);
	}

	// A frame of its own: every slot its storage, zeroed; each parameter its argument.
	bool MirInterpreter::Invoke(const FunctionRecord& callee, const HIRFunction& body, uint64 self, std::vector<CtfeBytes> arguments,
		const SourceLocation& at, CtfeBytes& result)
	{
		if (!body.mir || !body.complete) return Fail("'" + NameOf(callee) + "' cannot run at compile time: its body does not compile", at);
		if (m_Depth >= m_Limits.depth)
			return Fail("compile-time evaluation of '" + NameOf(callee) + "' went more than " +
				Core::String(std::to_string(m_Limits.depth).c_str()) + " calls deep: a recursion that does not end?", at);
		++m_Depth;
		Frame frame;
		frame.fn = &body;
		frame.self = self;
		frame.slots.resize(body.SlotCount());
		for (uint32 i = 0; i < body.SlotCount(); ++i)
		{
			const HIRFunction::Slot& slot = body.GetSlot(i);
			if (slot.cls == SlotClass::Static) return Fail("'" + NameOf(callee) + "' cannot run at compile time: it has a static local", at);
			frame.slots[i] = Allocate(slot.type ? slot.type->GetSize() : 0);
			if (slot.cls == SlotClass::Param && i < arguments.size())
				if (!Write(frame.slots[i], arguments[i], at)) return false;
		}
		const Exit exit = RunFrom(frame, MirBody::c_Entry);
		--m_Depth;
		if (exit != Exit::Return) return false;
		result = std::move(frame.result);
		return true;
	}

	MirInterpreter::Exit MirInterpreter::RunFrom(Frame& frame, MirBlockId start)
	{
		const MirBody& mir = *frame.fn->mir;
		for (MirBlockId block = start; block != c_MirNone; )
		{
			const MirBlock& b = mir.blocks[block];
			for (const MirStatement& statement : b.statements)
			{
				m_At = statement.at;
				if (!Step(statement.at)) return Exit::Failed;
				frame.regions.push_back({ statement.firstRegion, statement.regionCount });
				const bool ran = RunStatement(frame, statement);
				frame.regions.pop_back();
				if (!ran) return Exit::Failed;
			}
			m_At = b.end.at;
			if (!Step(b.end.at)) return Exit::Failed;
			frame.regions.push_back({ b.end.firstRegion, b.end.regionCount });
			Exit exit = Exit::Failed;
			block = RunTerminator(frame, b, exit);
			frame.regions.pop_back();
			if (block == c_MirNone) return exit;
		}
		return Exit::Failed;
	}

	bool MirInterpreter::RunStatement(Frame& frame, const MirStatement& statement)
	{
		CtfeBytes ignored;
		switch (statement.kind)
		{
		case MirStatement::Kind::Eval: return Eval(frame, statement.node, ignored);
		case MirStatement::Kind::Declare: return Declare(frame, *static_cast<const LocalDeclNode*>(statement.node));
		case MirStatement::Kind::SetFlag: return Write(frame.slots[statement.slot], CtfeBytes{ uint8(statement.flag ? 1 : 0) }, statement.at);
		case MirStatement::Kind::StageReturn: return !statement.node || Eval(frame, statement.node, frame.result);
		case MirStatement::Kind::Asm: return Fail("inline assembly cannot run at compile time", statement.at);
		case MirStatement::Kind::AtExit: return Fail("registering a destruction at exit cannot run at compile time", statement.at);
		case MirStatement::Kind::OnceEnd: return Fail("a static local cannot start at compile time", statement.at);
		}
		return false;
	}

	MirBlockId MirInterpreter::RunTerminator(Frame& frame, const MirBlock& block, Exit& exit)
	{
		const MirTerminator& end = block.end;
		const MirBody& mir = *frame.fn->mir;
		exit = Exit::Failed;
		switch (end.kind)
		{
		case MirTerminator::Kind::Goto: return end.target;
		case MirTerminator::Kind::Branch:
		{
			CtfeBytes condition;
			if (!Eval(frame, end.node, condition)) return c_MirNone;
			bool taken = false;
			for (uint8 byte : condition) taken = taken || byte != 0;
			if (end.node->type && TypeHandle::UnwrapToValue(end.node->type)->IsFloatingPoint())
				taken = Decode(condition, end.node->type).floatValue != 0.0;
			return taken ? end.target : end.otherwise;
		}
		case MirTerminator::Kind::Switch:
		{
			CtfeBytes value;
			if (!Eval(frame, end.node, value)) return c_MirNone;
			const int64 selected = Decode(value, end.node->type).AsInt();
			for (uint32 i = 0; i < end.caseCount; ++i)
				if (mir.cases[end.firstCase + i].value == selected) return mir.cases[end.firstCase + i].target;
			return end.otherwise;
		}
		case MirTerminator::Kind::Return:
			if (end.node && !Eval(frame, end.node, frame.result)) return c_MirNone;
			exit = Exit::Return;
			return c_MirNone;
		case MirTerminator::Kind::ReturnStaged:
		case MirTerminator::Kind::FallOff:
			exit = Exit::Return;
			return c_MirNone;
		case MirTerminator::Kind::RegionEnd:
			exit = Exit::RegionEnd;
			return c_MirNone;
		case MirTerminator::Kind::Throw:
			Fail(static_cast<const ThrowNode*>(end.node)->isRethrow ? "a rethrow cannot run at compile time"
				: "compile-time evaluation threw an exception", end.at);
			return c_MirNone;
		case MirTerminator::Kind::OnceBegin: Fail("a static local cannot start at compile time", end.at); return c_MirNone;
		case MirTerminator::Kind::Unreachable: Fail("compile-time evaluation reached code that cannot run", end.at); return c_MirNone;
		default: Fail("compile-time evaluation reached an exception's way out", end.at); return c_MirNone;
		}
	}

	// A block an expression runs in place: the region its statement made of it, run to its end.
	bool MirInterpreter::RunRegion(Frame& frame, const BlockNode* block)
	{
		const MirBody& mir = *frame.fn->mir;
		if (!frame.regions.empty())
		{
			const auto [first, count] = frame.regions.back();
			for (uint32 i = 0; i < count; ++i)
				if (mir.regions[first + i].block == block) return RunFrom(frame, mir.regions[first + i].entry) == Exit::RegionEnd;
		}
		return Fail("compile-time evaluation found a block it has no region of (a compiler fault)", m_At);
	}
} // namespace C4Compiler
