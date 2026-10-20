#include "MacroConstants.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/APFloat.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>

namespace CBind
{
	namespace
	{
		const char* const kC4Keywords[] = {
			"namespace", "class", "struct", "interface", "enum", "union", "alias", "template", "extern", "funcptr", "public",
			"private", "protected", "virtual", "override", "extends", "const", "static", "if", "else", "switch", "case",
			"default", "fallthrough", "for", "while", "do", "break", "continue", "return", "defer", "goto", "try", "catch",
			"finally", "throw", "asm", "new", "delete", "this", "is", "as", "true", "false", "null", "nullptr",
		};

		// The keywords a constant may hold: the type names of a cast, and sizeof.
		const char* const kTypeKeywords[] = {
			"void", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "_Bool", "bool", "const",
			"wchar_t", "__int8", "__int16", "__int32", "__int64", "sizeof",
		};

		bool IsC4Keyword(llvm::StringRef name)
		{
			return std::any_of(std::begin(kC4Keywords), std::end(kC4Keywords), [&](const char* k) { return name == k; });
		}

		bool SafeToken(const clang::Token& token)
		{
			if (token.isOneOf(clang::tok::l_brace, clang::tok::r_brace, clang::tok::semi, clang::tok::hash, clang::tok::hashhash))
				return false;
			const char* keyword = clang::tok::getKeywordSpelling(token.getKind());
			return !keyword || std::any_of(std::begin(kTypeKeywords), std::end(kTypeKeywords), [&](const char* k) { return std::strcmp(keyword, k) == 0; });
		}

		std::string ForwardSlashes(std::string path)
		{
			std::replace(path.begin(), path.end(), '\\', '/');
			return path;
		}

		// A C4 string literal of `bytes`: printable ASCII as itself, the rest as octal escapes.
		std::string C4StringLiteral(llvm::StringRef bytes)
		{
			std::string out = "\"";
			for (unsigned char c : bytes)
			{
				if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
				else if (c >= 0x20 && c < 0x7F) out += static_cast<char>(c);
				else
				{
					char escape[8];
					std::snprintf(escape, sizeof(escape), "\\%03o", c);
					out += escape;
				}
			}
			return out + "\"";
		}

		// An integer constant typed as C4 types the C value: int32, uint32, int64 or uint64, written so the literal
		// itself has that type (the most negative values as a subtraction, since their magnitude does not fit).
		std::string IntegerConstant(const std::string& name, const llvm::APSInt& value, uint64_t bits, bool isUnsigned)
		{
			if (bits > 64) return "";
			if (isUnsigned)
			{
				const std::string digits = std::to_string(value.getZExtValue());
				return bits <= 32 ? "const uint32 " + name + " = " + digits + "u;\n" : "const uint64 " + name + " = " + digits + "ull;\n";
			}
			const int64_t v = value.getSExtValue();
			if (bits <= 32) return "const int32 " + name + " = " + (v == INT32_MIN ? std::string("-2147483647 - 1") : std::to_string(v)) + ";\n";
			return "const int64 " + name + " = " + (v == INT64_MIN ? std::string("-9223372036854775807LL - 1") : std::to_string(v) + "LL") + ";\n";
		}

		std::string FloatConstant(const std::string& name, const llvm::APFloat& value, bool single)
		{
			if (value.isNaN() || value.isInfinity()) return "";
			char text[64];
			std::snprintf(text, sizeof(text), single ? "%.9g" : "%.17g", single ? static_cast<double>(value.convertToFloat()) : value.convertToDouble());
			std::string literal = text;
			const size_t exponent = literal.find_first_of("eE");
			if (literal.find('.') == std::string::npos) literal.insert(exponent == std::string::npos ? literal.size() : exponent, ".0");
			return std::string("const ") + (single ? "float " : "double ") + name + " = " + literal + (single ? "f" : "") + ";\n";
		}

		// The C4 declaration of one evaluated `__cbind_NAME`; empty when it is not a constant C4 can hold.
		std::string Declaration(clang::ASTContext& ctx, const clang::VarDecl* var, const std::string& name)
		{
			const clang::Expr* init = var->getInit();
			if (const auto* text = dyn_cast<clang::StringLiteral>(init->IgnoreParenImpCasts()))
				return text->isOrdinary() || text->isUTF8() ? "const char* " + name + " = " + C4StringLiteral(text->getString()) + ";\n" : "";
			clang::Expr::EvalResult result;
			if (init->isValueDependent() || !init->EvaluateAsRValue(result, ctx)) return "";
			const clang::QualType type = var->getType().getCanonicalType();
			if (result.Val.isInt() && type->isBooleanType()) return "const bool " + name + " = " + (result.Val.getInt().getBoolValue() ? "true" : "false") + ";\n";
			if (result.Val.isInt() && type->isIntegerType())
				return IntegerConstant(name, result.Val.getInt(), ctx.getTypeSize(type), type->isUnsignedIntegerType());
			if (result.Val.isFloat() && type->isRealFloatingType())
				return FloatConstant(name, result.Val.getFloat(), ctx.getTypeSize(type) == 32);
			return "";
		}
	}

	std::vector<std::string> ConstantCandidates(const clang::Preprocessor& pp)
	{
		const clang::SourceManager& sources = pp.getSourceManager();
		std::vector<std::string> names;
		for (const auto& entry : pp.macros())
		{
			const clang::MacroInfo* info = pp.getMacroInfo(entry.first);
			if (!info || !info->isObjectLike() || info->isBuiltinMacro() || info->getNumTokens() == 0) continue;
			const clang::SourceLocation at = info->getDefinitionLoc();
			if (at.isInvalid() || sources.isWrittenInBuiltinFile(at) || sources.isWrittenInCommandLineFile(at)) continue;
			const llvm::StringRef name = entry.first->getName();
			if (name.starts_with("_") || IsC4Keyword(name)) continue;
			if (std::all_of(info->tokens_begin(), info->tokens_end(), SafeToken)) names.push_back(name.str());
		}
		std::sort(names.begin(), names.end());
		return names;
	}

	std::string ConstantDeclarations(const std::vector<std::string>& candidates, const std::unordered_set<std::string>& taken,
		const std::string& header, const std::vector<std::string>& clangArgs, bool cxx)
	{
		std::string code = "#include \"" + ForwardSlashes(header) + "\"\n";
		bool any = false;
		for (const std::string& name : candidates)
		{
			if (taken.count(name)) continue;
			code += "static const __typeof__((" + name + ")) __cbind_" + name + " = " + name + ";\n";
			any = true;
		}
		if (!any) return "";
		std::vector<std::string> args = clangArgs;
		args.push_back("-ferror-limit=0");
		args.push_back("-w");
		clang::IgnoringDiagConsumer quiet;
		std::unique_ptr<clang::ASTUnit> unit = clang::tooling::buildASTFromCodeWithArgs(code, args, cxx ? "cbind_constants.cpp" : "cbind_constants.c",
			"cbind", std::make_shared<clang::PCHContainerOperations>(), clang::tooling::getClangStripDependencyFileAdjuster(),
			clang::tooling::FileContentMappings(), &quiet);
		if (!unit) return "";
		clang::ASTContext& ctx = unit->getASTContext();
		std::string out;
		for (const clang::Decl* decl : ctx.getTranslationUnitDecl()->decls())
		{
			const auto* var = dyn_cast<clang::VarDecl>(decl);
			if (!var || var->isInvalidDecl() || !var->getInit() || !var->getName().starts_with("__cbind_")) continue;
			out += Declaration(ctx, var, var->getName().substr(8).str());
		}
		return out.empty() ? out : "// Constants from the header's object-like macros (BUG-284).\n" + out;
	}
} // namespace CBind
