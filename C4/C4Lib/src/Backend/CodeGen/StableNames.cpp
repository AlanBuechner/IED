#include "Backend/CodeGen/StableNames.h"
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/xxhash.h>
#include <cstdio>

namespace Scripting::StableNames
{
	namespace
	{
		void Append(std::string& out, uint64_t value) { out.append(reinterpret_cast<const char*>(&value), sizeof(value)); }

		void AppendText(std::string& out, llvm::StringRef text)
		{
			Append(out, text.size());
			out.append(text.data(), text.size());
		}

		void AppendType(std::string& out, const llvm::Type& type)
		{
			Append(out, type.getTypeID());
			if (const auto* integer = llvm::dyn_cast<llvm::IntegerType>(&type)) Append(out, integer->getBitWidth());
			else if (const auto* pointer = llvm::dyn_cast<llvm::PointerType>(&type)) Append(out, pointer->getAddressSpace());
			else if (const auto* array = llvm::dyn_cast<llvm::ArrayType>(&type))
			{
				Append(out, array->getNumElements());
				AppendType(out, *array->getElementType());
			}
			else if (const auto* structure = llvm::dyn_cast<llvm::StructType>(&type))
			{
				AppendText(out, structure->hasName() ? structure->getName() : llvm::StringRef());
				Append(out, structure->isPacked());
				Append(out, structure->getNumElements());
				for (const llvm::Type* element : structure->elements()) AppendType(out, *element);
			}
		}

		// The kinds reflection tables are made of are encoded by their parts; any other by its printed form.
		void AppendConstant(std::string& out, const llvm::Constant& value)
		{
			Append(out, value.getValueID());
			AppendType(out, *value.getType());
			if (const auto* global = llvm::dyn_cast<llvm::GlobalValue>(&value)) AppendText(out, global->getName());
			else if (const auto* integer = llvm::dyn_cast<llvm::ConstantInt>(&value))
			{
				const llvm::APInt& bits = integer->getValue();
				out.append(reinterpret_cast<const char*>(bits.getRawData()), bits.getNumWords() * sizeof(uint64_t));
			}
			else if (const auto* real = llvm::dyn_cast<llvm::ConstantFP>(&value)) Append(out, real->getValueAPF().bitcastToAPInt().getZExtValue());
			else if (const auto* data = llvm::dyn_cast<llvm::ConstantDataSequential>(&value)) AppendText(out, data->getRawDataValues());
			else if (const auto* aggregate = llvm::dyn_cast<llvm::ConstantAggregate>(&value))
				for (const llvm::Use& element : aggregate->operands()) AppendConstant(out, *llvm::cast<llvm::Constant>(element.get()));
			else if (!llvm::isa<llvm::ConstantPointerNull, llvm::ConstantAggregateZero, llvm::UndefValue>(value))
			{
				std::string printed;
				llvm::raw_string_ostream text(printed);
				value.print(text);
				AppendText(out, text.str());
			}
		}
	}

	std::string ForBytes(std::string_view prefix, std::string_view bytes)
	{
		char hash[24];
		snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(llvm::xxh3_64bits(llvm::StringRef(bytes.data(), bytes.size()))));
		return std::string(prefix) + "." + hash;
	}

	std::string ForConstant(std::string_view prefix, const llvm::Constant& value)
	{
		std::string encoded;
		AppendConstant(encoded, value);
		return ForBytes(prefix, encoded);
	}
}
