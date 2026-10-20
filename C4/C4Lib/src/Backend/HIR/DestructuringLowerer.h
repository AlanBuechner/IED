#pragma once
#include "Backend/HIR/LoweringContext.h"
#include "Parser/Nodes/Statements/DestructuringNode.h"

namespace Scripting
{
	/// `[float a, b, _] = value;` (D-34): the value - a return list - is evaluated once into a statement
	/// temporary (a copy when it names an existing list), then each element in order takes its value out of it:
	/// a new local starts from it, an existing location is assigned it, a discarded one is destroyed with the
	/// temporary.
	class DestructuringLowerer
	{
	public:
		explicit DestructuringLowerer(LoweringContext& ctx) : m_Ctx(ctx) {}

		QueryResult<void> Lower(const DestructuringNode& n, BlockNode* block);

	private:
		/// The right side: a value list as its values' own return list, anything else as written.
		QueryResult<Value> Source(const DestructuringNode& n);
		QueryResult<const ObjectType*> ListOf(const DestructuringNode& n, const Value& value);
		const LValue* Hold(const HIRNode* start, const ObjectType* list, BlockNode* block);
		/// The value of element `index`, moved out of the held list.
		Value Moved(const LValue* held, const ObjectType* list, uint32 index);
		QueryResult<void> Element(const DestructuringNode& n, const DestructuringNode::Element& element, const Value& value, BlockNode* block);
		QueryResult<void> Declare(const DestructuringNode& n, const DestructuringNode::Element& element, const Value& value, BlockNode* block);
		QueryResult<void> Assign(const DestructuringNode::Element& element, const Value& value, BlockNode* block);

		LoweringContext& m_Ctx;
	};
} // namespace Scripting
