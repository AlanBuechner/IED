include "value.ch"
include "token.ch"
include "errors.ch"

namespace Calc
{
	// Prefix - (to an int) or ! (to anything); a wrong type is thrown as ScriptError at `line`.
	Value ApplyUnary(TokenKind op, const Value& operand, int64 line);
	// An arithmetic, comparison or equality operator; + also joins two strings.
	Value ApplyBinary(TokenKind op, const Value& left, const Value& right, int64 line);
}
