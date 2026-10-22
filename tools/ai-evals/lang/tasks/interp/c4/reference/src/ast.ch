include "token.ch"
include "value.ch"

namespace Calc
{
	class Stmt;

	// --- Expressions -------------------------------------------------------------------------------------------

	enum class ExprKind { Literal, Variable, Unary, Binary, Logical, Call, Array, Index, Function }

	// An expression of the syntax tree; `kind` says which class it is, `line` is where its runtime errors are.
	class Expr
	{
		public ExprKind kind;
		public int64 line;

		[operator("create")] public void Init(ExprKind kind, int64 line) {
			this.kind = kind;
			this.line = line;
		}
	}

	// A number, a string, true, false or nil.
	class LiteralExpr : Expr
	{
		public Value value;

		[operator("create")] public void Init(int64 line, Value value) {
			Expr::Init(ExprKind::Literal, line);
			this.value = take(value);
		}
	}

	class VariableExpr : Expr
	{
		public Core::String name;

		[operator("create")] public void Init(int64 line, Core::StringView name) {
			Expr::Init(ExprKind::Variable, line);
			this.name = Core::String(name);
		}
	}

	// -operand or !operand.
	class UnaryExpr : Expr
	{
		public TokenKind op;
		public Expr* operand;

		[operator("create")] public void Init(int64 line, TokenKind op, Expr* operand) {
			Expr::Init(ExprKind::Unary, line);
			this.op = op;
			this.operand = operand;
		}
	}

	// An arithmetic, comparison or equality operator; its line is the operator's.
	class BinaryExpr : Expr
	{
		public TokenKind op;
		public Expr* left;
		public Expr* right;

		[operator("create")] public void Init(int64 line, TokenKind op, Expr* left, Expr* right) {
			Expr::Init(ExprKind::Binary, line);
			this.op = op;
			this.left = left;
			this.right = right;
		}
	}

	// && or ||, which evaluate their right side only when it decides the result.
	class LogicalExpr : Expr
	{
		public TokenKind op;
		public Expr* left;
		public Expr* right;

		[operator("create")] public void Init(int64 line, TokenKind op, Expr* left, Expr* right) {
			Expr::Init(ExprKind::Logical, line);
			this.op = op;
			this.left = left;
			this.right = right;
		}
	}

	// A call; its line is the opening parenthesis's.
	class CallExpr : Expr
	{
		public Expr* callee;
		public Core::Array<Expr*> arguments;

		[operator("create")] public void Init(int64 line, Expr* callee, Core::Array<Expr*> arguments) {
			Expr::Init(ExprKind::Call, line);
			this.callee = callee;
			this.arguments = take(arguments);
		}
	}

	// An array literal: `[1, 2, 3]`.
	class ArrayExpr : Expr
	{
		public Core::Array<Expr*> elements;

		[operator("create")] public void Init(int64 line, Core::Array<Expr*> elements) {
			Expr::Init(ExprKind::Array, line);
			this.elements = take(elements);
		}
	}

	// An element of an array, `array[index]`; its line is the opening bracket's.
	class IndexExpr : Expr
	{
		public Expr* array;
		public Expr* index;

		[operator("create")] public void Init(int64 line, Expr* array, Expr* index) {
			Expr::Init(ExprKind::Index, line);
			this.array = array;
			this.index = index;
		}
	}

	// A function literal, `fn name(a, b) { ... }`; `name` is empty for an anonymous one.
	class FunctionExpr : Expr
	{
		public Core::String name;
		public Core::Array<Core::String> parameters;
		public Core::Array<Stmt*> body;

		[operator("create")] public void Init(int64 line) { Expr::Init(ExprKind::Function, line); }
	}

	// --- Statements --------------------------------------------------------------------------------------------

	enum class StmtKind { Let, Assign, IndexAssign, Expression, Block, If, While, Function, Return }

	// A statement of the syntax tree; `kind` says which class it is.
	class Stmt
	{
		public StmtKind kind;
		public int64 line;

		[operator("create")] public void Init(StmtKind kind, int64 line) {
			this.kind = kind;
			this.line = line;
		}
	}

	// `let name = value;` declares a variable in the current scope.
	class LetStmt : Stmt
	{
		public Core::String name;
		public Expr* value;

		[operator("create")] public void Init(int64 line, Core::StringView name, Expr* value) {
			Stmt::Init(StmtKind::Let, line);
			this.name = Core::String(name);
			this.value = value;
		}
	}

	// `name = value;` sets an existing variable.
	class AssignStmt : Stmt
	{
		public Core::String name;
		public Expr* value;

		[operator("create")] public void Init(int64 line, Core::StringView name, Expr* value) {
			Stmt::Init(StmtKind::Assign, line);
			this.name = Core::String(name);
			this.value = value;
		}
	}

	// `array[index] = value;` sets an element of an array.
	class IndexAssignStmt : Stmt
	{
		public IndexExpr* target;
		public Expr* value;

		[operator("create")] public void Init(IndexExpr* target, Expr* value) {
			Stmt::Init(StmtKind::IndexAssign, target->line);
			this.target = target;
			this.value = value;
		}
	}

	// `expression;`, evaluated for its effects.
	class ExpressionStmt : Stmt
	{
		public Expr* expression;

		[operator("create")] public void Init(int64 line, Expr* expression) {
			Stmt::Init(StmtKind::Expression, line);
			this.expression = expression;
		}
	}

	// `{ statements }`, a scope of its own.
	class BlockStmt : Stmt
	{
		public Core::Array<Stmt*> statements;

		[operator("create")] public void Init(int64 line) { Stmt::Init(StmtKind::Block, line); }
	}

	// `if (condition) { ... }` with an optional else: null, a BlockStmt or an IfStmt (else if).
	class IfStmt : Stmt
	{
		public Expr* condition;
		public BlockStmt* thenBlock;
		public Stmt* elseBranch;

		[operator("create")] public void Init(int64 line, Expr* condition, BlockStmt* thenBlock) {
			Stmt::Init(StmtKind::If, line);
			this.condition = condition;
			this.thenBlock = thenBlock;
		}
	}

	// `while (condition) { ... }`
	class WhileStmt : Stmt
	{
		public Expr* condition;
		public BlockStmt* body;

		[operator("create")] public void Init(int64 line, Expr* condition, BlockStmt* body) {
			Stmt::Init(StmtKind::While, line);
			this.condition = condition;
			this.body = body;
		}
	}

	// `fn name(a) { ... }` declares a named function in the current scope.
	class FunctionStmt : Stmt
	{
		public FunctionExpr* function;

		[operator("create")] public void Init(FunctionExpr* function) {
			Stmt::Init(StmtKind::Function, function->line);
			this.function = function;
		}
	}

	// `return value;` leaves the function being called; a null value returns nil.
	class ReturnStmt : Stmt
	{
		public Expr* value;

		[operator("create")] public void Init(int64 line, Expr* value) {
			Stmt::Init(StmtKind::Return, line);
			this.value = value;
		}
	}
}
