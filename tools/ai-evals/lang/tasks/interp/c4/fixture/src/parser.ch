include "ast.ch"
include "lexer.ch"

namespace Calc
{
	// Builds the syntax tree of a script from its tokens: statements in parser.cf, expressions in
	// expressions.cf. Parse errors are thrown as ScriptError.
	class Parser
	{
		private Core::Array<Token> tokens;
		private int64 position;
		private int64 functionDepth;    // how many function bodies the parser is inside; return is allowed only in one

		// `tokens` end with an EndOfInput.
		[operator("create")] public void Init(Core::Array<Token> tokens) { this.tokens = take(tokens); }

		// Statements up to the end of the input.
		public Core::Array<Stmt*> ParseProgram();
		public Stmt* ParseStatement();
		// `{ statements }`
		public BlockStmt* ParseBlock();

		public Expr* ParseExpression();
		// Operands joined by binary operators that bind at least as tightly as `minPrecedence`.
		public Expr* ParseBinary(int32 minPrecedence);
		// The prefix operators - and !.
		public Expr* ParseUnary();
		// A primary expression and the calls after it: f(1)(2).
		public Expr* ParseCall();
		// A literal, a variable, a parenthesized expression or a function literal.
		public Expr* ParsePrimary();

		private Stmt* ParseLet();
		private Stmt* ParseIf();
		private Expr* ParseCondition();
		private Stmt* ParseFunctionStatement();
		private FunctionExpr* ParseFunction();
		private Core::Array<Core::String> ParseParameters();
		private Stmt* ParseReturn();
		private Stmt* ParseExpressionStatement();
		private Stmt* ParseAssignment(Expr* target);
		private Expr* ParseTerminatedExpression();
		private Core::Array<Expr*> ParseExpressionList(TokenKind closing);
		private Expr* Literal(Value value);
		private Expr* ParseGroup();

		private const Token& Peek() const;
		private const Token& PeekAt(int64 offset) const;
		private Token Advance();
		private bool Check(TokenKind kind) const;
		private bool Match(TokenKind kind);
		private Token Expect(TokenKind kind);
	}

	// A whole script read into its statements.
	Core::Array<Stmt*> Parse(Core::StringView source);
	// The precedence table: how tightly a binary operator binds, kPrecedenceNone for a token that is not one.
	int32 Precedence(TokenKind kind);
}
