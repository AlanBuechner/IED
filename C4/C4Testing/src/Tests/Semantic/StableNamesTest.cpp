#include "StableNamesTest.h"
#include "Backend/CodeGen/StableNames.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <memory>
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// The parts a reflection table's row may differ in: a global it points at, a count, a number and a null.
		struct Row
		{
			std::string global = "name.literal";
			uint64_t count = 3;
			double number = 1.5;
			bool pointsAtGlobal = true;
		};

		// A table of two rows {ptr, i32, double, ptr} (`wide`: the count is an i64), built in a module of its own.
		class Table
		{
		public:
			Table(const Row& second, bool wide = false) : m_Module("table", m_Context)
			{
				llvm::Type* ptr = llvm::PointerType::get(m_Context, 0);
				llvm::Type* count = wide ? llvm::Type::getInt64Ty(m_Context) : llvm::Type::getInt32Ty(m_Context);
				llvm::StructType* row = llvm::StructType::get(m_Context, { ptr, count, llvm::Type::getDoubleTy(m_Context), ptr });
				llvm::Constant* rows[] = { MakeRow(row, count, Row()), MakeRow(row, count, second) };
				m_Table = llvm::ConstantArray::get(llvm::ArrayType::get(row, 2), rows);
			}

			std::string Name() const { return StableNames::ForConstant("rows", *m_Table); }

		private:
			llvm::Constant* MakeRow(llvm::StructType* row, llvm::Type* count, const Row& parts)
			{
				llvm::Constant* fields[] = {
					parts.pointsAtGlobal ? Global(parts.global) : llvm::ConstantPointerNull::get(llvm::PointerType::get(m_Context, 0)),
					llvm::ConstantInt::get(count, parts.count),
					llvm::ConstantFP::get(llvm::Type::getDoubleTy(m_Context), parts.number),
					Global("shared"),
				};
				return llvm::ConstantStruct::get(row, fields);
			}

			llvm::Constant* Global(const std::string& name)
			{
				if (llvm::GlobalVariable* existing = m_Module.getGlobalVariable(name, true)) return existing;
				return new llvm::GlobalVariable(m_Module, llvm::Type::getInt8Ty(m_Context), true, llvm::GlobalValue::PrivateLinkage,
					llvm::ConstantInt::get(llvm::Type::getInt8Ty(m_Context), 0), name);
			}

			llvm::LLVMContext m_Context;
			llvm::Module m_Module;
			llvm::Constant* m_Table = nullptr;
		};
	}

	void StableNamesTest::TheSameConstantInAnotherContextHasTheSameName()
	{
		const std::string first = Table(Row()).Name();
		TEST_ASSERT(first == Table(Row()).Name());
		TEST_ASSERT(first.rfind("rows.", 0) == 0);
	}

	void StableNamesTest::EachPartOfAConstantChangesItsName()
	{
		const std::string base = Table(Row()).Name();
		Row global;
		global.global = "other.literal";
		Row count;
		count.count = 4;
		Row number;
		number.number = 2.5;
		Row null;
		null.pointsAtGlobal = false;
		for (const Row& changed : { global, count, number, null }) TEST_ASSERT(Table(changed).Name() != base);
	}

	void StableNamesTest::TheRowTypeChangesTheName()
	{
		TEST_ASSERT(Table(Row(), true).Name() != Table(Row()).Name());
	}
} // namespace Testing
