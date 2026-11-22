#include "AggregateRegistersTest.h"
#include "Target/AggregateRegisters.h"
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		struct Aggregates
		{
			llvm::LLVMContext context;
			llvm::DataLayout layout{ TargetAbi::For(TargetSystem::Linux).DataLayout() };

			llvm::Type* I8() { return llvm::Type::getInt8Ty(context); }
			llvm::Type* I32() { return llvm::Type::getInt32Ty(context); }
			llvm::Type* I64() { return llvm::Type::getInt64Ty(context); }
			llvm::Type* Float() { return llvm::Type::getFloatTy(context); }
			llvm::Type* Double() { return llvm::Type::getDoubleTy(context); }
			llvm::Type* Floats2() { return llvm::FixedVectorType::get(Float(), 2); }

			llvm::StructType* Struct(const std::vector<llvm::Type*>& fields, bool packed = false) { return llvm::StructType::get(context, fields, packed); }

			// `storage` as the convention sees it: its size from the layout.
			llvm::Type* SystemV(llvm::Type* storage)
			{
				return SystemVAggregateRegisters({ context, layout, layout.getTypeAllocSize(storage), [storage] { return storage; } });
			}

			llvm::Type* Win64(uint64_t size)
			{
				return Win64AggregateRegisters({ context, layout, size, [] { return static_cast<llvm::Type*>(nullptr); } });
			}
		};
	}

	void AggregateRegistersTest::Win64PassesOneRegisterSizesAsAnInteger()
	{
		Aggregates a;
		TEST_ASSERT(a.Win64(1) == llvm::Type::getInt8Ty(a.context));
		TEST_ASSERT(a.Win64(2) == llvm::Type::getInt16Ty(a.context));
		TEST_ASSERT(a.Win64(4) == a.I32());
		TEST_ASSERT(a.Win64(8) == a.I64());
	}

	void AggregateRegistersTest::Win64PassesEveryOtherSizeInMemory()
	{
		Aggregates a;
		for (uint64_t size : { 0, 3, 5, 6, 7, 12, 16, 24 }) TEST_ASSERT(a.Win64(size) == nullptr);
	}

	void AggregateRegistersTest::Win64NeverLooksAtTheFields()
	{
		Aggregates a;
		bool asked = false;
		llvm::Type* registers = Win64AggregateRegisters({ a.context, a.layout, 8, [&] { asked = true; return a.Struct({ a.Float(), a.Float() }); } });
		TEST_ASSERT(registers == a.I64() && !asked); // two floats are still one integer register on Windows
	}

	void AggregateRegistersTest::SystemVPassesFloatsInSseRegisters()
	{
		Aggregates a;
		TEST_ASSERT(a.SystemV(a.Struct({ a.Float() })) == a.Float());
		TEST_ASSERT(a.SystemV(a.Struct({ a.Float(), a.Float() })) == a.Floats2());
		TEST_ASSERT(a.SystemV(a.Struct({ a.Double(), a.Double() })) == a.Struct({ a.Double(), a.Double() }));
		TEST_ASSERT(a.SystemV(llvm::ArrayType::get(a.Float(), 4)) == a.Struct({ a.Floats2(), a.Floats2() }));
	}

	void AggregateRegistersTest::SystemVMixesIntegersIntoAnIntegerEightbyte()
	{
		Aggregates a;
		TEST_ASSERT(a.SystemV(a.Struct({ a.I32(), a.Float() })) == a.I64()); // INTEGER and SSE in one eightbyte
		TEST_ASSERT(a.SystemV(a.Struct({ a.I8(), a.I8(), a.I8() })) == llvm::Type::getIntNTy(a.context, 24)); // the bytes it holds
	}

	void AggregateRegistersTest::SystemVClassifiesEachEightbyteAlone()
	{
		Aggregates a;
		TEST_ASSERT(a.SystemV(a.Struct({ a.I64(), a.Double() })) == a.Struct({ a.I64(), a.Double() }));
		// The tail padding rides along: the second eightbyte is all 8 bytes, in the same register clang's i32 takes.
		TEST_ASSERT(a.SystemV(a.Struct({ a.Double(), a.I32() })) == a.Struct({ a.Double(), a.I64() }));
		TEST_ASSERT(a.SystemV(a.Struct({ a.Float(), a.Float(), a.Float() })) == a.Struct({ a.Floats2(), a.Float() }));
	}

	void AggregateRegistersTest::SystemVPassesLargeOrUnalignedAggregatesInMemory()
	{
		Aggregates a;
		TEST_ASSERT(a.SystemV(a.Struct({ a.I64(), a.I64(), a.I64() })) == nullptr); // over 16 bytes
		TEST_ASSERT(a.SystemV(a.Struct({ llvm::Type::getX86_FP80Ty(a.context) })) == nullptr); // x87
		TEST_ASSERT(a.SystemV(a.Struct({ a.I8(), a.I32() }, true)) == nullptr); // an unaligned field
		TEST_ASSERT(SystemVAggregateRegisters({ a.context, a.layout, 0, [&] { return static_cast<llvm::Type*>(a.Struct({})); } }) == nullptr);
	}

	void AggregateRegistersTest::EachTargetUsesItsConvention()
	{
		Aggregates a;
		llvm::Type* twoFloats = a.Struct({ a.Float(), a.Float() });
		const auto storage = [twoFloats] { return twoFloats; };
		const AggregateShape shape{ a.context, a.layout, 8, storage };
		TEST_ASSERT(TargetAbi::For(TargetSystem::Windows).AggregateRegisters(shape) == a.I64());
		TEST_ASSERT(TargetAbi::For(TargetSystem::Linux).AggregateRegisters(shape) == a.Floats2());
	}
} // namespace Testing
