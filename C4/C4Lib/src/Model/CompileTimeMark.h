#pragma once

namespace C4Compiler
{
	struct AnnotationItem;
	struct FunctionRecord;

	/// D-91: the `[compile_time]` mark on one of a function's declarations, or null.
	const AnnotationItem* CompileTimeMark(const FunctionRecord& record);
	inline bool IsCompileTime(const FunctionRecord& record) { return CompileTimeMark(record) != nullptr; }
} // namespace C4Compiler
