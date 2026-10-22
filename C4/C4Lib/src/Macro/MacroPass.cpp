#include "MacroPass.h"
#include "MacroPreprocessor.h"
#include "Compiler/FileIo.h"
#include <algorithm>

namespace Scripting
{
	static std::string Norm(const std::string& p)
	{
		std::string r = p;
		std::replace(r.begin(), r.end(), '\\', '/');
		return r;
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
			out.push_back(std::move(done.at(abs))); // each file's text and tables, moved: every file is in `done` once
		return out;
	}

	// Through FileIo, so a deep output folder is written (BUG-356).
	void MacroPass::ExportTables(const std::vector<FileResult>& results, const std::string& outDir)
	{
		CreateFolders(outDir);
		for (const auto& r : results)
		{
			if (r.table.Empty()) continue;
			std::string base = Norm(r.name);
			size_t dot = base.rfind('.');
			if (dot != std::string::npos) base = base.substr(0, dot);
			const std::string outPath = outDir + "/" + base + ".c4macro";
			CreateFolders(outPath.substr(0, outPath.find_last_of('/')));
			WriteFileBytes(outPath, r.table.ToExportText());
		}
	}
}
