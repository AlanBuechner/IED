#include "Backend/CodeGen/StableNames.h"
#include <llvm/ADT/SmallString.h>
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
		// What a hash is made of: a few words on the stack for most constants.
		using Bytes = llvm::SmallString<128>;

		void AppendRaw(Bytes& out, const void* data, size_t size)
		{
			const char* bytes = static_cast<const char*>(data);
			out.append(bytes, bytes + size);
		}

		void Append(Bytes& out, uint64_t value) { AppendRaw(out, &value, sizeof(value)); }

		void AppendText(Bytes& out, llvm::StringRef text)
		{
			Append(out, text.size());
			AppendRaw(out, text.data(), text.size());
		}

		void AppendType(Bytes& out, const llvm::Type& type)
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
	}

	// The kinds reflection tables are made of are encoded by their parts (an aggregate by its elements' hashes); any
	// other by its printed form. A global is its name, which may change, so only aggregates are remembered.
	uint64_t ConstantNamer::Hash(const llvm::Constant& value)
	{
		const auto* aggregate = llvm::dyn_cast<llvm::ConstantAggregate>(&value);
		if (aggregate)
			if (auto known = m_Aggregates.find(&value); known != m_Aggregates.end()) return known->second;
		Bytes out;
		StableNames::Append(out, value.getValueID());
		StableNames::Append(out, TypeHash(*value.getType()));
		if (const auto* global = llvm::dyn_cast<llvm::GlobalValue>(&value)) AppendText(out, global->getName());
		else if (const auto* integer = llvm::dyn_cast<llvm::ConstantInt>(&value))
		{
			const llvm::APInt& bits = integer->getValue();
			AppendRaw(out, bits.getRawData(), bits.getNumWords() * sizeof(uint64_t));
		}
		else if (const auto* real = llvm::dyn_cast<llvm::ConstantFP>(&value)) StableNames::Append(out, real->getValueAPF().bitcastToAPInt().getZExtValue());
		else if (const auto* data = llvm::dyn_cast<llvm::ConstantDataSequential>(&value)) AppendText(out, data->getRawDataValues());
		else if (aggregate)
			for (const llvm::Use& element : aggregate->operands()) StableNames::Append(out, Hash(*llvm::cast<llvm::Constant>(element.get())));
		else if (!llvm::isa<llvm::ConstantPointerNull, llvm::ConstantAggregateZero, llvm::UndefValue>(value))
		{
			std::string printed;
			llvm::raw_string_ostream text(printed);
			value.print(text);
			AppendText(out, text.str());
		}
		const uint64_t hash = llvm::xxh3_64bits(out.str());
		if (aggregate) m_Aggregates.emplace(&value, hash);
		return hash;
	}

	std::string ForBytes(std::string_view prefix, std::string_view bytes)
	{
		char hash[24];
		snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(llvm::xxh3_64bits(llvm::StringRef(bytes.data(), bytes.size()))));
		return std::string(prefix) + "." + hash;
	}

	uint64_t ConstantNamer::TypeHash(const llvm::Type& type)
	{
		auto [entry, fresh] = m_Types.try_emplace(&type);
		if (fresh)
		{
			Bytes bytes;
			AppendType(bytes, type);
			entry->second = llvm::xxh3_64bits(bytes.str());
		}
		return entry->second;
	}

	std::string ConstantNamer::Name(std::string_view prefix, const llvm::Constant& value)
	{
		char hash[24];
		snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(Hash(value)));
		return std::string(prefix) + "." + hash;
	}

	std::string ForConstant(std::string_view prefix, const llvm::Constant& value)
	{
		return ConstantNamer().Name(prefix, value);
	}
}
