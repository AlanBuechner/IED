#include "Compiler/ProjectFile.h"
#include "Compiler/FileIo.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace Scripting
{
	namespace
	{
		struct Token
		{
			enum class Kind { Key, String, Open, Close, Equals, Comma, Dot, Newline, End };
			Kind kind = Kind::End;
			std::string text;
			uint32_t line = 1;
		};

		bool IsKeyCharacter(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }

		// The tokens of `text`, or the line and reason it holds something else.
		std::string Tokenize(const std::string& text, std::vector<Token>& out)
		{
			uint32_t line = 1;
			for (size_t at = 0; at < text.size();)
			{
				const char c = text[at];
				auto add = [&](Token::Kind kind, std::string value = "") { out.push_back({ kind, std::move(value), line }); };
				if (c == '\n') { add(Token::Kind::Newline); ++line; ++at; continue; }
				if (c == ' ' || c == '\t' || c == '\r') { ++at; continue; }
				if (c == '#') { while (at < text.size() && text[at] != '\n') ++at; continue; }
				if (c == '[') { add(Token::Kind::Open); ++at; continue; }
				if (c == ']') { add(Token::Kind::Close); ++at; continue; }
				if (c == '=') { add(Token::Kind::Equals); ++at; continue; }
				if (c == ',') { add(Token::Kind::Comma); ++at; continue; }
				if (c == '.') { add(Token::Kind::Dot); ++at; continue; }
				if (c == '"')
				{
					std::string value;
					for (++at; at < text.size() && text[at] != '"'; ++at)
					{
						if (text[at] == '\n') return "line " + std::to_string(line) + ": a string ends at the end of its line";
						if (text[at] != '\\') { value += text[at]; continue; }
						if (++at >= text.size()) break;
						switch (text[at])
						{
						case '\\': value += '\\'; break;
						case '"': value += '"'; break;
						case 'n': value += '\n'; break;
						case 't': value += '\t'; break;
						default: return "line " + std::to_string(line) + ": '\\" + text[at] + "' is not an escape this file takes (\\\\, \\\", \\n, \\t)";
						}
					}
					if (at >= text.size()) return "line " + std::to_string(line) + ": a string has no closing '\"'";
					++at;
					add(Token::Kind::String, value);
					continue;
				}
				if (IsKeyCharacter(c))
				{
					size_t end = at;
					while (end < text.size() && IsKeyCharacter(text[end])) ++end;
					add(Token::Kind::Key, text.substr(at, end - at));
					at = end;
					continue;
				}
				return "line " + std::to_string(line) + ": unexpected '" + std::string(1, c) + "'";
			}
			out.push_back({ Token::Kind::End, "", line });
			return "";
		}

		class Parser
		{
		public:
			Parser(std::vector<Token> tokens, fs::path folder) : m_Tokens(std::move(tokens)), m_Folder(std::move(folder)) {}

			std::string Run(ProjectList& list)
			{
				while (Peek().kind != Token::Kind::End)
				{
					if (Peek().kind == Token::Kind::Newline) { ++m_At; continue; }
					std::string why = Peek().kind == Token::Kind::Open ? Header(list) : KeyValue(list);
					if (!why.empty()) return "line " + std::to_string(m_Line) + ": " + why;
				}
				return "";
			}

		private:
			enum class Table { Top, Project, Defines };

			const Token& Peek() const { return m_Tokens[m_At]; }
			const Token& Take() { m_Line = m_Tokens[m_At].line; return m_Tokens[m_At++]; }
			bool Accept(Token::Kind kind) { if (Peek().kind != kind) return false; Take(); return true; }
			void SkipNewlines() { while (Peek().kind == Token::Kind::Newline) Take(); }

			std::string EndOfLine()
			{
				if (Peek().kind == Token::Kind::Newline || Peek().kind == Token::Kind::End) return "";
				Take();
				return "a line holds one table header or one 'key = value'";
			}

			std::string Header(ProjectList& list)
			{
				Take();
				const bool arrayTable = Accept(Token::Kind::Open);
				std::string name;
				do
				{
					if (Peek().kind != Token::Kind::Key) return "a table header names a table";
					name += (name.empty() ? "" : ".") + Take().text;
				} while (Accept(Token::Kind::Dot));
				if (!Accept(Token::Kind::Close) || (arrayTable && !Accept(Token::Kind::Close))) return "a table header is not closed";
				if (arrayTable && name == "project")
				{
					list.projects.emplace_back();
					m_Table = Table::Project;
					m_Seen.clear();
				}
				else if (!arrayTable && name == "project.defines")
				{
					if (list.projects.empty()) return "[project.defines] comes after the [[project]] it belongs to";
					m_Table = Table::Defines;
				}
				else return "'" + std::string(arrayTable ? "[[" : "[") + name + (arrayTable ? "]]" : "]") + "' is not a table of a build description";
				return EndOfLine();
			}

			std::string KeyValue(ProjectList& list)
			{
				if (Peek().kind != Token::Kind::Key && Peek().kind != Token::Kind::String) return "expected 'key = value'";
				const std::string key = Take().text;
				if (!Accept(Token::Kind::Equals)) return "'" + key + "' needs '= value'";
				std::vector<std::string> values;
				bool isArray = false;
				if (std::string why = Value(values, isArray); !why.empty()) return why;
				if (std::string why = Store(list, key, values, isArray); !why.empty()) return why;
				return EndOfLine();
			}

			std::string Value(std::vector<std::string>& values, bool& isArray)
			{
				if (Peek().kind == Token::Kind::String) { values.push_back(Take().text); return ""; }
				if (!Accept(Token::Kind::Open)) return "a value is a \"string\" or a [\"list\", \"of strings\"]";
				isArray = true;
				SkipNewlines();
				while (!Accept(Token::Kind::Close))
				{
					if (Peek().kind != Token::Kind::String) return "a list holds \"strings\" separated by commas";
					values.push_back(Take().text);
					SkipNewlines();
					if (Accept(Token::Kind::Comma)) SkipNewlines();
					else if (Peek().kind != Token::Kind::Close) return "a list holds \"strings\" separated by commas";
				}
				return "";
			}

			std::string Store(ProjectList& list, const std::string& key, const std::vector<std::string>& values, bool isArray)
			{
				if (m_Table == Table::Defines)
				{
					if (isArray) return "a define's value is one \"string\"";
					list.projects.back().defines[key] = values[0];
					return "";
				}
				if (m_Table == Table::Top)
				{
					if (key != "root") return "'" + key + "' is not a key of a build description (root, [[project]])";
					if (isArray) return "root is the \"name\" of one project";
					list.root = values[0];
					return "";
				}
				if (!m_Seen.insert(key).second) return "'" + key + "' is set twice in one project";
				Project& project = list.projects.back();
				if (key == "name") { if (isArray) return "name is one \"string\""; project.name = values[0]; return ""; }
				if (!isArray && (key == "sources" || key == "includes" || key == "depends")) return key + " is a [\"list\"]";
				if (key == "sources") { for (const auto& v : values) project.sources.push_back(Path(v)); return ""; }
				if (key == "includes") { for (const auto& v : values) project.includes.push_back(Path(v)); return ""; }
				if (key == "depends") { project.depends = values; return ""; }
				return "'" + key + "' is not a key of a project (name, sources, includes, depends, [project.defines])";
			}

			fs::path Path(const std::string& written) const
			{
				const fs::path path(written);
				return (path.is_absolute() ? path : m_Folder / path).lexically_normal();
			}

			std::vector<Token> m_Tokens;
			fs::path m_Folder;
			size_t m_At = 0;
			uint32_t m_Line = 1;
			Table m_Table = Table::Top;
			std::set<std::string> m_Seen;
		};
	}

	ProjectFileResult ParseProjectFile(const std::string& text, const fs::path& folder)
	{
		ProjectFileResult result;
		std::vector<Token> tokens;
		result.error = Tokenize(text, tokens);
		if (result.error.empty()) result.error = Parser(std::move(tokens), folder).Run(result.list);
		return result;
	}

	// Through FileIo, so a project in a deep folder reads (BUG-356).
	ProjectFileResult ReadProjectFile(const fs::path& file)
	{
		std::string shown = PathText(file);
		std::replace(shown.begin(), shown.end(), '\\', '/');
		if (!IsFile(PathText(file))) return { {}, "cannot read the build description '" + shown + "'" };
		ProjectFileResult result = ParseProjectFile(ReadFileBytes(PathText(file)), fs::absolute(file).parent_path());
		if (!result.error.empty()) result.error = shown + ": " + result.error;
		return result;
	}
}
