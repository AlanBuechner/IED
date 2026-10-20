#include "Protocol/LspTypes.h"
#include "Lexer/Token.h" // Scripting::SourceLocation
#include <algorithm>

namespace C4LSP::Protocol
{
	Position Position::FromSourceLocation(const Scripting::SourceLocation& loc)
	{
		Position p;
		p.line = loc.line > 0 ? loc.line - 1 : 0;
		p.character = loc.column > 0 ? loc.column - 1 : 0;
		return p;
	}

	Range Range::FromSourceRange(const Scripting::SourceLocation& start, const Scripting::SourceLocation& end)
	{
		Range r;
		r.start = Position::FromSourceLocation(start);
		r.end = Position::FromSourceLocation(end);
		return r;
	}

	std::string DocumentUri::ToUri(const std::string& absolutePath)
	{
		std::string path = absolutePath;
		std::replace(path.begin(), path.end(), '\\', '/');
		if (!path.empty() && path[0] == '/')
			return "file://" + path; // already a UNC-style /C:/... form

		if (path.size() >= 2 && path[1] == ':')
			return "file:///" + path; // C:\... -> file:///C:/...

		return "file://" + path;
	}

	std::string DocumentUri::FromUri(const std::string& uri)
	{
		const std::string prefix = "file://";
		if (uri.compare(0, prefix.size(), prefix) != 0)
			return uri;

		std::string path = uri.substr(prefix.size());
#ifdef _WIN32
		// file:///C:/... -> C:/... ; file://server/share -> //server/share
		if (!path.empty() && path[0] == '/')
			path.erase(0, 1);
#endif
		// Editors percent-encode reserved characters (Monaco sends "file:///c%3A/Users/...", with a
		// lowercase drive letter too). Decode so the result is a real absolute path; case-insensitive
		// matching against registered sources happens in CompilationSession.
		std::string decoded;
		decoded.reserve(path.size());
		for (size_t i = 0; i < path.size(); ++i)
			if (path[i] == '%' && i + 2 < path.size() && isxdigit(static_cast<unsigned char>(path[i + 1])) && isxdigit(static_cast<unsigned char>(path[i + 2])))
			{
				auto hex = [](char c) { return static_cast<int>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10); };
				decoded.push_back(static_cast<char>(hex(path[i + 1]) * 16 + hex(path[i + 2])));
				i += 2;
			}
			else decoded.push_back(path[i]);
		path = std::move(decoded);

		std::replace(path.begin(), path.end(), '/', '\\');
		return path;
	}

	Location Location::Make(const std::string& uri, const Scripting::SourceLocation& start, const Scripting::SourceLocation& end)
	{
		Location loc;
		loc.uri = uri;
		loc.range = Range::FromSourceRange(start, end);
		return loc;
	}
} // namespace C4LSP::Protocol
