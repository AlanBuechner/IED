#pragma once
#include "Compiler/ErrorTracker.h"
#include "Lexer/Token.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace Scripting
{
	class AstNode;
	class ObjectType;
	class ProgramModel;
	class Type;
	struct FunctionRecord;
	struct Scope;
	struct SymbolInfo;

	/// Which of a program's declarations runtime reflection registers (D-89). All of them unless a setting takes some out:
	/// `[reflect("off")]` on a declaration, on a type (its members with it) or on a namespace block (what the block
	/// declares), `reflect = "off"` in a project's description (the project's declarations); `[reflect("on")]` brings one
	/// back. The nearest setting decides: the declaration's own, its type's for a member, a template instance's template's,
	/// the namespace blocks around it from the innermost out, its project's; the default library's files are in. An entity
	/// out of scope is not registered and has no invoke or type thunks, and a global gets storage only when code names it;
	/// the census and the collector's roots do not change. Its answers are the same in C4.exe, C4LSP and a script compile:
	/// it reads only the model.
	class ReflectionScope
	{
	public:
		explicit ReflectionScope(const ProgramModel& model);

		bool Has(const Type& type) const;
		bool Has(const FunctionRecord& function) const;
		bool Has(const SymbolInfo& variable) const;

		/// What took `type` out of the scope, for a diagnostic ("its [reflect("off")] at shapes.ch (3:1)", "project
		/// 'Engine' says reflect = "off""); empty when it is in.
		std::string WhyOut(const Type& type) const;

		/// Each `[reflect]` written with anything but one "on" or "off", once.
		const ErrorTracker& Errors() const { return m_Errors; }

	private:
		enum class Setting { Unset, On, Off };
		struct Decision
		{
			bool in = true;
			std::string why; // what took it out
		};
		struct Block
		{
			SourceLocation start;
			SourceLocation end;
			Setting setting = Setting::Unset;
			SourceLocation written;
		};

		Decision Decide(const std::vector<const AstNode*>& declarations, const ObjectType* owner) const;
		Decision OfType(const Type& type) const;
		Decision Around(const AstNode* declaration) const;
		Setting Written(const AstNode* declaration, SourceLocation* where) const;
		Setting Written(const Core::Vector<Ref<AnnotationNode>>& annotations, SourceLocation* where) const;

		const ProgramModel& m_Model;
		std::unordered_map<uint32, std::vector<Block>> m_Blocks; // each unit's namespace blocks that say [reflect]
		mutable std::unordered_map<const Type*, Decision> m_Types;
		mutable ErrorTracker m_Errors;
		mutable std::unordered_map<uint64, bool> m_Reported; // a misused [reflect] by its place, reported once
	};
}
