include "output.ch"

namespace Calc
{
	class Environment;
	class FunctionExpr;
	class BuiltinValue;
	class FunctionValue;

	enum class ValueKind { Nil, Int, Bool, String, Function, Builtin }

	// A value of a script; `kind` says which of the other fields holds it.
	struct Value
	{
		public ValueKind kind;
		public int64 integer;
		public bool boolean;
		public Core::String text;
		public FunctionValue* function;
		public BuiltinValue* builtin;

		public static Value Nil();
		public static Value Int(int64 integer);
		public static Value Bool(bool boolean);
		public static Value Str(Core::StringView text);
		public static Value Function(FunctionValue* function);
		public static Value Builtin(BuiltinValue* builtin);

		// "int", "string", "bool", "nil" or "function", as errors name the type.
		public Core::StringView TypeName() const;
	}

	// A function of the script together with its closure: the environment it was defined in, whose variables
	// its body can read and assign.
	class FunctionValue
	{
		public FunctionExpr* declaration;
		public Environment* closure;

		[operator("create")] public void Init(FunctionExpr* declaration, Environment* closure) {
			this.declaration = declaration;
			this.closure = closure;
		}
	}

	// What a builtin function is given: its arguments (already counted against its arity), the line of the
	// call for errors, and where print writes.
	struct BuiltinCall
	{
		public Core::Array<Value> arguments;
		public int64 line;
		public IOutput* output;
	}

	const int64 kAnyArgumentCount = -1;

	// A function the interpreter provides, such as print.
	class BuiltinValue
	{
		public Core::String name;
		public int64 arity;     // kAnyArgumentCount: any number of arguments
		public funcptr<Value(const BuiltinCall&)> call;

		[operator("create")] public void Init(Core::StringView name, int64 arity, funcptr<Value(const BuiltinCall&)> call) {
			this.name = Core::String(name);
			this.arity = arity;
			this.call = call;
		}
	}

	// Whether a condition holds for `value`: everything but false and nil does.
	bool IsTruthy(const Value& value);
	// ==: values of different types are never equal; functions are equal only to themselves.
	bool ValuesEqual(const Value& a, const Value& b);
	// How print writes a value: strings without quotes, functions as <fn name>.
	void AppendValue(Core::StringBuilder& out, const Value& value);
	Core::String FormatValue(const Value& value);
}
