#pragma once
#include "Core/Core.h"
#include "Features/ReferenceScanner.h"
#include <vector>

namespace Scripting { class AstNode; class ObjectType; class ProgramModel; }
namespace C4LSP::Server { class SemanticResolver; }

namespace C4LSP::Features
{
	/// Where code builds an object through its type's create operator (docs/language/06 6.2): `new T(args)`, a variable
	/// `T x(args)` or `T x;`, and a temporary `T(args)`. A braced `T{...}` fills the fields and calls none; a field is
	/// built with the object that holds it.
	class Constructions
	{
	public:
		struct Construction
		{
			const Scripting::ObjectType* type = nullptr;
			uint32 argumentCount = 0;
			ReferenceScanner::NameRange name; // the type's name where it is written; the variable's for `T x(args)`
		};

		/// The constructions `node` makes (`path` ends at it): one, or for a variable definition one a declarator.
		static Core::Vector<Construction> Of(const Scripting::AstNode& node, const Core::Vector<const Scripting::AstNode*>& path,
			const Server::SemanticResolver& resolver);

		/// The declarations of the create operators the construction may call: those of its type that take its count of
		/// arguments (the LSP types no argument, so overloads of one count all fit).
		static std::vector<const Scripting::AstNode*> Called(const Scripting::ProgramModel& model, const Construction& construction);
	};
} // namespace C4LSP::Features
