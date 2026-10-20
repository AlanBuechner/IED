#pragma once
/// Umbrella header for all C4 declaration AST node types.
/// Includes every declaration node definition so consumers need only include this one file.
/// Must be included AFTER Parser/AstNode.h (each individual node header already does this).

#include "Parser/Nodes/Declarations/IncludeDeclarationNode.h"
#include "Parser/Nodes/Declarations/ExternDeclarationNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
