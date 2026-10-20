#include "MacroPass.h"
#include "MacroPreprocessor.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

namespace Scripting
{
	static std::string Norm(const std::string& p)
	{
		std::string r = p;
		std::replace(r.begin(), r.end(), '\\', '/');
		return r;
	}

	static std::string NormalizeSegments(const std::string& path)
	{
		std::vector<std::string> parts;
		size_t pos = 0;
		while (pos <= path.size())
		{
			size_t next = path.find('/', pos);
			std::string seg = (next == std::string::npos) ? path.substr(pos) : path.substr(pos, next - pos);
			if (!seg.empty() && seg != ".")
			{
				if (seg == ".." && !parts.empty()) parts.pop_back();
				else if (seg != "..") parts.push_back(seg);
			}
			if (next == std::string::npos) break;
			pos = next + 1;
		}
		std::string out;
		for (size_t i = 0; i < parts.size(); ++i)
			out += (i ? "/" : "") + parts[i];
		return out;
	}

	static std::string ReadRaw(const fs::path& path)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f.is_open()) return "";
		std::ostringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}

	namespace
	{
		// The .cf/.ch files under `folder`, absolute path -> text, and `folder` added to `roots`.
		void CollectFolder(const std::string& folder, std::map<std::string, std::string>& raw, std::vector<std::string>& roots)
		{
			std::string abs = Norm(fs::absolute(folder).string());
			if (std::find(roots.begin(), roots.end(), abs) == roots.end())
				roots.push_back(abs);
			for (const auto& entry : fs::recursive_directory_iterator(folder))
			{
				if (!entry.is_regular_file()) continue;
				const auto ext = entry.path().extension().string();
				if (ext != ".cf" && ext != ".ch") continue;
				raw[Norm(fs::absolute(entry.path()).string())] = ReadRaw(entry.path());
			}
		}
	}

	const MacroPass::FileResult& MacroPass::ProcessOne(const Context& ctx, const std::string& absPath,
		std::map<std::string, FileResult>& done, std::vector<std::string>& inProgress)
	{
		auto it = done.find(absPath);
		if (it != done.end()) return it->second;

		const Input& input = ctx.files.at(absPath);
		FileResult result;
		result.name = input.name;
		result.absPath = absPath;
		if (input.expanded)
		{
			result.text = input.text;
			result.table = input.exports;
			result.exported = input.exports;
			return done.emplace(absPath, std::move(result)).first->second;
		}
		for (const auto& [name, body] : input.predefined)
			result.table.Define(name, body);

		auto inheritMacros = [&](const std::string& includePath)
		{
			std::string incNorm = Norm(includePath);
			if (incNorm.empty()) return;
			const std::string resolved = ctx.resolve(absPath, incNorm);
			if (resolved.empty() || !ctx.files.count(resolved)) return; // unresolved include - the compiler reports it

			bool alreadyInChain = std::find(inProgress.begin(), inProgress.end(), resolved) != inProgress.end();
			if (alreadyInChain) return;    // cycle guard: skip, don't recurse

			// Merge the included file's final macro state into this file's table. The merge
			// happens here (not inside ProcessOne) so it applies even when the include was
			// already processed and returned from the cache.
			inProgress.push_back(resolved);
			const FileResult& incResult = ProcessOne(ctx, resolved, done, inProgress);
			inProgress.pop_back();
			result.table.Merge(incResult.exported);
		};

		PreprocessedFile processed = MacroPreprocessor::Process(input.text, result.name, result.table, inheritMacros);
		result.text = std::move(processed.text);
		result.diagnostics = std::move(processed.diagnostics);
		result.exported = result.table.Without(input.predefined);
		return done.emplace(absPath, std::move(result)).first->second;
	}

	std::vector<MacroPass::FileResult> MacroPass::RunFiles(std::vector<Input> files, const IncludeResolver& resolve)
	{
		Context ctx;
		ctx.resolve = resolve;
		for (Input& file : files)
		{
			const std::string abs = Norm(file.absPath);
			ctx.files[abs] = std::move(file);
		}

		std::map<std::string, FileResult> done;
		for (const auto& [abs, file] : ctx.files)
		{
			std::vector<std::string> inProgress = { abs };
			ProcessOne(ctx, abs, done, inProgress);
		}

		std::vector<FileResult> out;
		out.reserve(done.size());
		for (const auto& [abs, file] : ctx.files)
			out.push_back(done.at(abs));
		return out;
	}

	std::vector<MacroPass::FileResult> MacroPass::RunFolders(const std::vector<std::string>& roots,
		const std::map<std::string, std::string>* overrides,
		const std::map<std::string, std::string>& predefinedMacros)
	{
		std::map<std::string, std::string> raw;
		std::vector<std::string> rootFolders;
		for (const auto& root : roots)
			CollectFolder(root, raw, rootFolders);
		if (overrides)
			for (const auto& [abs, text] : *overrides)
				if (raw.count(Norm(abs))) raw[Norm(abs)] = text;

		std::vector<Input> files;
		for (const auto& [abs, text] : raw)
		{
			Input input;
			input.absPath = abs;
			input.name = rootFolders.empty() ? fs::path(abs).filename().string() : fs::relative(abs, rootFolders.front()).generic_string();
			input.text = text;
			input.predefined = predefinedMacros;
			files.push_back(std::move(input));
		}
		// Relative to the including file's directory, then each root folder.
		auto resolve = [&raw, rootFolders](const std::string& including, const std::string& include) -> std::string
		{
			std::vector<std::string> candidates = { NormalizeSegments(Norm(fs::path(including).parent_path().string()) + "/" + include) };
			for (const auto& root : rootFolders) candidates.push_back(NormalizeSegments(root + "/" + include));
			for (const auto& candidate : candidates)
				if (raw.count(candidate)) return candidate;
			return "";
		};
		return RunFiles(std::move(files), resolve);
	}

	void MacroPass::ExportTables(const std::vector<FileResult>& results, const std::string& outDir)
	{
		fs::create_directories(outDir);
		for (const auto& r : results)
		{
			if (r.table.Empty()) continue;
			std::string base = Norm(r.name);
			size_t dot = base.rfind('.');
			if (dot != std::string::npos) base = base.substr(0, dot);
			fs::path outPath = fs::path(outDir) / (base + ".c4macro");
			std::ofstream f(outPath);
			if (f.is_open()) f << r.table.ToExportText();
		}
	}
}
