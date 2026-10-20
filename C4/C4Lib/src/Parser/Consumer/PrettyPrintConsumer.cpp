#include "Parser/Consumer/PrettyPrintConsumer.h"
#include <iostream>
#include <windows.h>
#include <string>

namespace Scripting
{
	PrettyPrintConsumer::PrettyPrintConsumer()
	{
	}

	/// Ensure the Windows console uses UTF-8 code page so box-drawing characters render.
	static void EnsureConsoleUtf8()
	{
		static bool initialized = false;
		if (!initialized)
		{
			SetConsoleOutputCP(65001); // UTF-8
			initialized = true;
		}
	}

	void PrettyPrintConsumer::HandleTranslationUnit(const Ref<AstNode>& root)
	{
		EnsureConsoleUtf8();
		std::cout << "=== AST Tree ===" << std::endl;
		if (!root)
		{
			std::cout << "(null root)" << std::endl;
			return;
		}

		auto label = NodeLabel(*root);
		if (label.empty())
			label = "[TranslationUnit]";
		std::cout << label << std::endl;

		auto children = root->Children();
		for (int i = 0; i < static_cast<int>(children.size()); ++i)
		{
			bool isLast = (i == static_cast<int>(children.size()) - 1);
			PrintTree(children[i], "", isLast);
		}

		std::cout << std::endl;
	}

	bool PrettyPrintConsumer::PreVisit(const AstNode& node)
	{
		(void)node;
		return true; // unused -- we do our own recursive walk
	}

	void PrettyPrintConsumer::PostVisit(const AstNode& node)
	{
		(void)node;
		// unused -- we do our own recursive walk
	}

	void PrettyPrintConsumer::PrintTree(const Ref<AstNode>& node, const std::string& prefix, bool isLast)
	{
		if (!node) return;

		std::string connector = isLast ? "--- " : "+-- ";
		auto label = NodeLabel(*node);
		if (label.empty())
			label = "[Unknown]";
		std::cout << prefix << connector << label << std::endl;

		auto children = node->Children();
		std::string childPrefix = prefix + (isLast ? "    " : "|   ");
		for (int i = 0; i < static_cast<int>(children.size()); ++i)
		{
			bool childIsLast = (i == static_cast<int>(children.size()) - 1);
			PrintTree(children[i], childPrefix, childIsLast);
		}
	}

	std::string PrettyPrintConsumer::NodeLabel(const AstNode& node)
	{
		return Core::StringFormat::Format("[{0}] {1}", NodeTypeToName(node.Type()), node.ToString());
	}
}
