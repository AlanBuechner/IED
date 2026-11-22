#include "Backend/MIR/MirText.h"

namespace C4Compiler
{
	namespace
	{
		std::string Block(MirBlockId block) { return block == c_MirNone ? "none" : "bb" + std::to_string(block); }
		std::string Step(MirStepId step) { return step == c_MirNone ? "caller" : "s" + std::to_string(step); }

		std::string Regions(uint32 count) { return count ? " (" + std::to_string(count) + " region" + (count > 1 ? "s)" : ")") : ""; }

		std::string Statement(const MirStatement& statement)
		{
			switch (statement.kind)
			{
			case MirStatement::Kind::Eval: return "eval" + Regions(statement.regionCount);
			case MirStatement::Kind::Declare:
				return "declare %" + std::to_string(static_cast<const LocalDeclNode*>(statement.node)->slot) + Regions(statement.regionCount);
			case MirStatement::Kind::Asm: return "asm";
			case MirStatement::Kind::AtExit: return "at exit";
			case MirStatement::Kind::SetFlag: return "flag %" + std::to_string(statement.slot) + (statement.flag ? " = true" : " = false");
			case MirStatement::Kind::StageReturn: return std::string("stage return") + (statement.node ? " value" : "") + Regions(statement.regionCount);
			case MirStatement::Kind::OnceEnd: return "once end";
			}
			return "?";
		}

		std::string Terminator(const MirBody& mir, const MirTerminator& end)
		{
			switch (end.kind)
			{
			case MirTerminator::Kind::Open: return "open";
			case MirTerminator::Kind::Goto: return "goto " + Block(end.target);
			case MirTerminator::Kind::Branch: return "branch " + Block(end.target) + " " + Block(end.otherwise) + Regions(end.regionCount);
			case MirTerminator::Kind::Switch:
			{
				std::string text = "switch";
				for (uint32 i = 0; i < end.caseCount; ++i)
					text += " " + std::to_string(mir.cases[end.firstCase + i].value) + ":" + Block(mir.cases[end.firstCase + i].target);
				return text + " else " + Block(end.otherwise) + Regions(end.regionCount);
			}
			case MirTerminator::Kind::Return: return std::string("return") + (end.node ? " value" : "") + Regions(end.regionCount);
			case MirTerminator::Kind::ReturnStaged: return "return staged";
			case MirTerminator::Kind::FallOff: return "fall off";
			case MirTerminator::Kind::Throw:
				return static_cast<const ThrowNode*>(end.node)->isRethrow ? "rethrow" : "throw" + Regions(end.regionCount);
			case MirTerminator::Kind::Unreachable: return "unreachable";
			case MirTerminator::Kind::LeaveHandler: return "leave h" + std::to_string(end.handler) + " to " + Block(end.target);
			case MirTerminator::Kind::Unwind: return "unwind to " + Step(end.step);
			case MirTerminator::Kind::OnceBegin: return "once " + Block(end.target) + " else " + Block(end.otherwise);
			case MirTerminator::Kind::RegionEnd: return "region end";
			case MirTerminator::Kind::Terminate: return "terminate";
			}
			return "?";
		}

		std::string Header(MirBlockId id, const MirBlock& block)
		{
			std::string notes;
			const auto note = [&](const std::string& text) { notes += (notes.empty() ? " (" : ", ") + text; };
			if (block.mayUnwind) note("unwind " + Step(block.unwind));
			if (block.funclet != c_MirNone) note("funclet f" + std::to_string(block.funclet));
			if (block.handler != c_MirNone) note("handler h" + std::to_string(block.handler));
			if (block.cleanupOf) note("cleanup");
			if (block.inPlace) note("in place");
			return Block(id) + (notes.empty() ? "" : notes + ")") + ":\n";
		}

		std::string StepText(const MirBody& mir, MirStepId id)
		{
			const MirStep& step = mir.steps[id];
			std::string text = Step(id) + " ";
			switch (step.kind)
			{
			case MirStep::Kind::Cleanup: text += "cleanup " + Block(step.body); break;
			case MirStep::Kind::Abandon: text += "abandon"; break;
			case MirStep::Kind::Dispatch:
				text += "dispatch";
				for (uint32 i = 0; i < step.handlerCount; ++i) text += " h" + std::to_string(step.firstHandler + i);
				break;
			case MirStep::Kind::EndCatch: text += "end catch h" + std::to_string(step.handler); break;
			}
			return text + " then " + Step(step.next) + "\n";
		}
	}

	std::string MirText(const MirBody& mir)
	{
		std::string text;
		for (MirBlockId id = 0; id < mir.blocks.size(); ++id)
		{
			const MirBlock& block = mir.blocks[id];
			text += Header(id, block);
			for (const MirStatement& statement : block.statements) text += "  " + Statement(statement) + "\n";
			text += "  " + Terminator(mir, block.end) + "\n";
		}
		for (MirStepId id = 0; id < mir.steps.size(); ++id) text += StepText(mir, id);
		for (uint32 h = 0; h < mir.handlers.size(); ++h)
		{
			const MirHandler& handler = mir.handlers[h];
			const bool all = !handler.clause || handler.clause->catchAll;
			text += "h" + std::to_string(h) + " " + Block(handler.entry) + (handler.clause ? (all ? " catch all" : " catch") : " terminate") + "\n";
		}
		return text;
	}
} // namespace C4Compiler
