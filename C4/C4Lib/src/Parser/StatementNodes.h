#pragma once
/// Umbrella header for all C4 statement AST node types.
/// Includes every statement node definition so consumers need only include this one file.
/// Must be included AFTER Parser/AstNode.h (each individual node header already does this).

#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Parser/Nodes/Statements/ExpressionStatementNode.h"
#include "Parser/Nodes/Statements/EmptyStatementNode.h"
