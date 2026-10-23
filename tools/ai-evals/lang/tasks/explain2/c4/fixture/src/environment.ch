include "value.ch"

namespace Calc
{
	// One scope of variables. `parent` is the scope it is nested in: the enclosing block, or for a function
	// call the function's closure; null for the global scope.
	class Environment
	{
		private Core::HashMap<Core::String, Value> values;
		public Environment* parent;

		[operator("create")] public void Init(Environment* parent) { this.parent = parent; }

		// Creates `name` in this scope, or replaces it when this scope already has it.
		public void Define(Core::StringView name, Value value);
		// Sets `name` in the nearest scope that has it; false when none does.
		public bool Assign(Core::StringView name, const Value& value);
		// Reads `name` from the nearest scope that has it; false when none does.
		public bool Lookup(Core::StringView name, Value& value);
	}
}
