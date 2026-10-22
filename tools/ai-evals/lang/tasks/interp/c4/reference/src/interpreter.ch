include "ast.ch"
include "environment.ch"
include "errors.ch"

namespace Calc
{
	// How a statement ended: normally, or by a return and the value returned.
	struct Flow
	{
		public bool returned;
		public Value value;
	}

	// Runs a parsed script by walking its syntax tree; what the script prints goes to `output`. Runtime errors
	// are thrown as ScriptError.
	class Interpreter
	{
		public Environment* globals;
		private IOutput* output;

		// The global scope starts with the builtins.
		[operator("create")] public void Init(IOutput* output);

		// Executes a script's statements in the global scope.
		public void Run(const Core::Array<Stmt*>& program);
		// Executes statements in order in `env`, stopping at a return.
		public Flow ExecuteBlock(const Core::Array<Stmt*>& statements, Environment* env);
		public Flow Execute(Stmt* stmt, Environment* env);
		public Value Evaluate(Expr* expr, Environment* env);
		// Calls a function of the script with its arguments; `line` is the call's.
		public Value CallFunction(FunctionValue* function, const Core::Array<Value>& arguments, int64 line);

		private void ExecuteLet(LetStmt* stmt, Environment* env);
		private void ExecuteAssign(AssignStmt* stmt, Environment* env);
		private void ExecuteIndexAssign(IndexAssignStmt* stmt, Environment* env);
		private Flow ExecuteIf(IfStmt* stmt, Environment* env);
		private Flow ExecuteWhile(WhileStmt* stmt, Environment* env);
		private Flow ExecuteReturn(ReturnStmt* stmt, Environment* env);
		private Value LookupVariable(VariableExpr* expr, Environment* env);
		private Value EvaluateBinary(BinaryExpr* expr, Environment* env);
		private Value EvaluateLogical(LogicalExpr* expr, Environment* env);
		private Value EvaluateCall(CallExpr* expr, Environment* env);
		private Value EvaluateArray(ArrayExpr* expr, Environment* env);
		private Value EvaluateIndex(IndexExpr* expr, Environment* env);
		private Core::Array<Value> EvaluateAll(const Core::Array<Expr*>& exprs, Environment* env);
		private Value CallBuiltin(BuiltinValue* builtin, const Core::Array<Value>& arguments, int64 line);
	}

	// Parses and runs a script, writing what it prints to `output`; a lex, parse or runtime error is thrown as
	// ScriptError.
	void RunScript(Core::StringView source, IOutput* output);
}
