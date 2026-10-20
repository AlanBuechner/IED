#pragma once
/// Umbrella header for all C4 expression AST node types.
/// Includes every expression node definition so consumers need only include this one file.
/// Must be included AFTER Parser/AstNode.h (each individual node header already does this).

#include "Parser/Nodes/Expressions/LiteralNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/BinaryExpressionNode.h"
#include "Parser/Nodes/Expressions/UnaryExpressionNode.h"
#include "Parser/Nodes/Expressions/TernaryExpressionNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Parser/Nodes/Expressions/ArrayIndexNode.h"
#include "Parser/Nodes/Expressions/AssignmentExpressionNode.h"
#include "Parser/Nodes/Expressions/ConditionalAndNode.h"
#include "Parser/Nodes/Expressions/ConditionalOrNode.h"
#include "Parser/Nodes/Expressions/TypeCheckNode.h"
#include "Parser/Nodes/Expressions/SafeCastNode.h"
#include "Parser/Nodes/Expressions/CastExpressionNode.h"
#include "Parser/Nodes/Expressions/ParenthesizedExpressionNode.h"
