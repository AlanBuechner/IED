#include "Target/Engine.h"
#include "Target/Expression.h"
#include "Target/ScalarText.h"
#include "Target/WideText.h"

namespace C4DAP::Target
{
	namespace
	{
		constexpr size_t kMaxFrames = 256;
		// What Visual Studio shows for a variable optimized code keeps nowhere at the stop.
		constexpr const char* kOptimizedAway = "<optimized away>";
		// A local in a register the adapter cannot read.
		constexpr const char* kUnreadable = "<unreadable>";

		bool OptimizedAway(const VariableSymbol& symbol) { return symbol.storage.kind == SymbolStorage::Kind::OptimizedAway; }
	}

	void Engine::ClearStopTables()
	{
		m_Frames.clear();
		m_FramesOfThread.clear();
		m_References.clear();
		m_InlinedLeftOut = 0;
	}

	std::vector<ThreadInfo> Engine::Threads() const
	{
		std::vector<ThreadInfo> threads;
		for (DWORD id : m_Process.Threads())
		{
			if (m_BreakInThreads.count(id)) continue;
			ThreadInfo thread{ id, id == m_Process.MainThread() ? "main" : "thread " + std::to_string(id) };
			PWSTR description = nullptr;
			if (SUCCEEDED(GetThreadDescription(m_Process.Thread(id), &description)) && description)
			{
				if (*description) thread.name = Narrow(description);
				LocalFree(description);
			}
			threads.push_back(std::move(thread));
		}
		return threads;
	}

	std::vector<FrameInfo> Engine::StackTrace(DWORD thread, uint32_t start, uint32_t count, uint32_t& total)
	{
		total = 0;
		if (m_State != State::Stopped || !m_Process.Thread(thread)) return {};
		std::vector<uint32_t>& ids = m_FramesOfThread[thread];
		if (ids.empty()) ids = RecordFrames(thread);
		total = static_cast<uint32_t>(ids.size());
		std::vector<FrameInfo> page;
		for (uint32_t i = start; i < ids.size() && (count == 0 || page.size() < count); ++i)
		{
			const FrameRecord& record = m_Frames[ids[i] - 1];
			FrameInfo frame{ ids[i], AddressText(record.frame.pc), m_Symbols.PositionOf(record.Place()), AddressText(record.frame.pc) };
			if (const std::optional<FunctionFacts> function = m_Symbols.FunctionOf(record.Place())) frame.name = function->name;
			page.push_back(std::move(frame));
		}
		return page;
	}

	// Each call's inlined calls come before it, innermost first, as frames of their own; the stopped thread's top
	// call leaves out those its stop shows it outside of.
	std::vector<uint32_t> Engine::RecordFrames(DWORD thread)
	{
		std::vector<uint32_t> ids;
		const std::vector<FrameRegisters> walked = WalkStack(m_Process, thread, kMaxFrames);
		for (size_t depth = 0; depth < walked.size(); ++depth)
		{
			const size_t leftOut = depth == 0 && thread == m_Held.dwThreadId ? m_InlinedLeftOut : 0;
			for (size_t inner = leftOut; inner < walked[depth].inlined.size(); ++inner)
			{
				m_Frames.push_back(FrameRecord{ thread, depth, walked[depth], walked[depth].inlined[inner] });
				ids.push_back(static_cast<uint32_t>(m_Frames.size()));
			}
			m_Frames.push_back(FrameRecord{ thread, depth, walked[depth] });
			ids.push_back(static_cast<uint32_t>(m_Frames.size()));
		}
		return ids;
	}

	uint32_t Engine::LocalsOf(uint32_t frameId)
	{
		if (frameId == 0 || frameId > m_Frames.size()) return 0;
		return Remember(LocalsReference{ frameId - 1 });
	}

	uint32_t Engine::Remember(const Reference& reference)
	{
		m_References.push_back(reference);
		return static_cast<uint32_t>(m_References.size());
	}

	VariableInfo Engine::Describe(const std::string& name, const ValueRef& value)
	{
		VariableInfo variable{ name, m_Values.Text(value), m_Values.TypeName(value.type) };
		if (m_Values.HasChildren(value)) variable.reference = Remember(value);
		return variable;
	}

	VariableInfo Engine::DescribeLocal(const VariableSymbol& symbol, const FrameRegisters& frame)
	{
		if (OptimizedAway(symbol)) return VariableInfo{ symbol.name, kOptimizedAway, m_Values.TypeName(symbol.type) };
		const std::optional<ValueRef> value = VariableValue(symbol, frame);
		if (!value) return VariableInfo{ symbol.name, kUnreadable, m_Values.TypeName(symbol.type) };
		return Describe(symbol.name, *value);
	}

	std::vector<VariableInfo> Engine::Variables(uint32_t reference)
	{
		std::vector<VariableInfo> variables;
		if (m_State != State::Stopped || reference == 0 || reference > m_References.size()) return variables;
		const Reference target = m_References[reference - 1];
		if (const auto* locals = std::get_if<LocalsReference>(&target))
		{
			const FrameRecord record = m_Frames[locals->frame];
			for (const VariableSymbol& symbol : m_Symbols.VariablesAt(record.Place()))
				variables.push_back(DescribeLocal(symbol, record.frame));
			return variables;
		}
		for (const NamedValue& child : m_Values.Children(std::get<ValueRef>(target))) variables.push_back(Describe(child.name, child.value));
		return variables;
	}

	std::optional<ValueRef> Engine::Variable(const std::string& name, const FrameRecord* frame) const
	{
		if (frame)
			for (const VariableSymbol& symbol : m_Symbols.VariablesAt(frame->Place()))
				if (symbol.name == name) return VariableValue(symbol, frame->frame);
		if (const std::optional<VariableSymbol> global = m_Symbols.Global(name)) return ValueRef{ global->type, static_cast<uint64_t>(global->storage.offset) };
		return std::nullopt;
	}

	std::string Engine::Unavailable(const std::string& name, const FrameRecord* frame) const
	{
		if (frame)
			for (const VariableSymbol& symbol : m_Symbols.VariablesAt(frame->Place()))
				if (symbol.name == name) return name + (OptimizedAway(symbol) ? " is optimized away here" : " is unreadable here");
		return "no variable named " + name;
	}

	EvaluateResult Engine::Evaluate(const std::string& text, uint32_t frameId)
	{
		EvaluateResult result;
		if (m_State != State::Stopped) { result.error = "the program is not stopped"; return result; }
		const std::optional<Expression> expression = ParseExpression(text, result.error);
		if (!expression) return result;
		const FrameRecord* frame = frameId > 0 && frameId <= m_Frames.size() ? &m_Frames[frameId - 1] : nullptr;
		std::optional<ValueRef> value = Variable(expression->variable, frame);
		if (!value) { result.error = Unavailable(expression->variable, frame); return result; }
		std::string spelled = expression->variable;
		for (const PathStep& step : expression->path)
		{
			if (step.kind == PathStep::Kind::Index)
			{
				spelled += "[" + std::to_string(step.index) + "]";
				value = m_Values.Element(*value, step.index);
			}
			else
			{
				spelled += (step.kind == PathStep::Kind::Field ? "." : "->") + step.field;
				value = m_Values.Member(*value, step.field);
			}
			if (!value) { result.error = spelled + " is not a value of the program"; return result; }
		}
		for (uint32_t i = 0; i < expression->dereferences; ++i)
		{
			value = m_Values.Target(*value);
			if (!value) { result.error = spelled + " does not point at a value"; return result; }
		}
		result.value = Describe(text, *value);
		return result;
	}
} // namespace C4DAP::Target
