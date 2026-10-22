#include "Script/HostInterface.h"
#include "Compiler/CpuTarget.h"
#include "Compiler/FileIo.h"
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/xxhash.h>
#include <cstring>
#include <iterator>

namespace Scripting
{
	namespace
	{
		constexpr char kMagic[4] = { 'C', '4', 'I', 'F' };
		constexpr uint32 kFormatVersion = 4;
		constexpr size_t kBuildIdOffset = sizeof(kMagic) + sizeof(uint32); // after the magic and the version

		// Little-endian fields: integers of fixed width, strings and lists after their length.
		class Writer
		{
		public:
			void U8(uint8 value) { m_Out.push_back(static_cast<char>(value)); }
			void U32(uint32 value)
			{
				for (int i = 0; i < 4; ++i) U8(static_cast<uint8>(value >> (8 * i)));
			}
			void I32(int32 value) { U32(static_cast<uint32>(value)); }
			void Bool(bool value) { U8(value ? 1 : 0); }
			void Bytes(const void* data, size_t size) { m_Out.append(static_cast<const char*>(data), size); }
			void Text(const std::string& text)
			{
				U32(static_cast<uint32>(text.size()));
				Bytes(text.data(), text.size());
			}
			void Texts(const std::vector<std::string>& texts)
			{
				U32(static_cast<uint32>(texts.size()));
				for (const std::string& text : texts) Text(text);
			}
			void Indexes(const std::vector<uint32>& indexes)
			{
				U32(static_cast<uint32>(indexes.size()));
				for (uint32 index : indexes) U32(index);
			}
			std::string Take() { return std::move(m_Out); }

		private:
			std::string m_Out;
		};

		// Reads what Writer wrote; once a read runs past the end every later one fails too.
		class Reader
		{
		public:
			explicit Reader(std::string_view bytes) : m_Bytes(bytes) {}

			bool Ok() const { return m_Ok; }
			bool AtEnd() const { return m_At == m_Bytes.size(); }

			uint8 U8()
			{
				if (!Need(1)) return 0;
				return static_cast<uint8>(m_Bytes[m_At++]);
			}
			uint32 U32()
			{
				if (!Need(4)) return 0;
				uint32 value = 0;
				for (int i = 0; i < 4; ++i) value |= static_cast<uint32>(static_cast<uint8>(m_Bytes[m_At++])) << (8 * i);
				return value;
			}
			int32 I32() { return static_cast<int32>(U32()); }
			bool Bool() { return U8() != 0; }
			bool Bytes(void* out, size_t size)
			{
				if (!Need(size)) return false;
				std::memcpy(out, m_Bytes.data() + m_At, size);
				m_At += size;
				return true;
			}
			std::string Text()
			{
				const uint32 size = U32();
				if (!Need(size)) return {};
				std::string text(m_Bytes.substr(m_At, size));
				m_At += size;
				return text;
			}
			std::vector<std::string> Texts()
			{
				const uint32 count = U32();
				std::vector<std::string> texts;
				for (uint32 i = 0; i < count && m_Ok; ++i) texts.push_back(Text());
				return texts;
			}
			std::vector<uint32> Indexes()
			{
				const uint32 count = U32();
				std::vector<uint32> indexes;
				for (uint32 i = 0; i < count && m_Ok; ++i) indexes.push_back(U32());
				return indexes;
			}

		private:
			bool Need(size_t size)
			{
				if (m_Ok && m_Bytes.size() - m_At >= size) return true;
				m_Ok = false;
				return false;
			}

			std::string_view m_Bytes;
			size_t m_At = 0;
			bool m_Ok = true;
		};

		void WriteMacro(Writer& out, const MacroDefinition& macro)
		{
			out.Text(macro.name);
			out.Bool(macro.isFunctionLike);
			out.Texts(macro.params);
			out.Text(macro.body);
		}

		MacroDefinition ReadMacro(Reader& in)
		{
			MacroDefinition macro;
			macro.name = in.Text();
			macro.isFunctionLike = in.Bool();
			macro.params = in.Texts();
			macro.body = in.Text();
			return macro;
		}

		void WriteFile(Writer& out, const InterfaceFile& file)
		{
			out.U32(file.unitId);
			out.U32(file.project);
			out.I32(file.folder);
			out.Text(file.path);
			out.Bool(file.library);
			out.Bool(file.prelude);
			out.Text(file.text);
			out.U32(static_cast<uint32>(file.macros.size()));
			for (const MacroDefinition& macro : file.macros) WriteMacro(out, macro);
		}

		InterfaceFile ReadFile(Reader& in)
		{
			InterfaceFile file;
			file.unitId = in.U32();
			file.project = in.U32();
			file.folder = in.I32();
			file.path = in.Text();
			file.library = in.Bool();
			file.prelude = in.Bool();
			file.text = in.Text();
			const uint32 macros = in.U32();
			for (uint32 i = 0; i < macros && in.Ok(); ++i) file.macros.push_back(ReadMacro(in));
			return file;
		}

		void WriteProject(Writer& out, const InterfaceProject& project)
		{
			out.Text(project.name);
			out.Indexes(project.dependencies);
			out.Indexes(project.folders);
		}

		InterfaceProject ReadProject(Reader& in)
		{
			InterfaceProject project;
			project.name = in.Text();
			project.dependencies = in.Indexes();
			project.folders = in.Indexes();
			return project;
		}

		bool Fail(std::string* error, const std::string& why)
		{
			if (error) *error = why;
			return false;
		}
	}

	std::string SerializeInterface(const HostInterface& host)
	{
		Writer out;
		out.Bytes(kMagic, sizeof(kMagic));
		out.U32(kFormatVersion);
		out.Bytes(host.buildId.data(), host.buildId.size());
		out.U32(host.compilerVersion);
		out.Text(host.targetCpu);
		out.Text(host.targetFeatures);
		out.U32(host.debugInfo ? 1u : 0u);
		out.U32(static_cast<uint32>(host.projects.size()));
		for (const InterfaceProject& project : host.projects) WriteProject(out, project);
		out.U32(host.rootProject);
		out.U32(host.folderCount);
		out.U32(static_cast<uint32>(host.files.size()));
		for (const InterfaceFile& file : host.files) WriteFile(out, file);
		out.Texts(host.symbols);
		out.Texts(host.reflectedTypes);
		out.Texts(host.inlineBodies);
		return out.Take();
	}

	bool DeserializeInterface(std::string_view bytes, HostInterface& host, std::string* error)
	{
		Reader in(bytes);
		char magic[4] = {};
		if (!in.Bytes(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0)
			return Fail(error, "not a C4 interface (.c4i) file");
		const uint32 version = in.U32();
		if (version != kFormatVersion)
			return Fail(error, "the interface is format " + std::to_string(version) + "; this script host reads format " + std::to_string(kFormatVersion));
		HostInterface read;
		in.Bytes(read.buildId.data(), read.buildId.size());
		read.compilerVersion = in.U32();
		read.targetCpu = in.Text();
		read.targetFeatures = in.Text();
		read.debugInfo = in.U32() != 0;
		const uint32 projects = in.U32();
		for (uint32 i = 0; i < projects && in.Ok(); ++i) read.projects.push_back(ReadProject(in));
		read.rootProject = in.U32();
		read.folderCount = in.U32();
		const uint32 files = in.U32();
		for (uint32 i = 0; i < files && in.Ok(); ++i) read.files.push_back(ReadFile(in));
		read.symbols = in.Texts();
		read.reflectedTypes = in.Texts();
		read.inlineBodies = in.Texts();
		if (!in.Ok()) return Fail(error, "the interface ends early (a truncated or damaged .c4i file)");
		if (!in.AtEnd()) return Fail(error, "the interface has bytes past its end (a damaged .c4i file)");
		std::string why;
		if (!CpuTarget::FromLlvm(read.targetCpu, read.targetFeatures, why)) return Fail(error, "the host's target: " + why);
		host = std::move(read);
		return true;
	}

	std::string SealInterface(HostInterface& host)
	{
		host.buildId = {};
		std::string bytes = SerializeInterface(host);
		const llvm::XXH128_hash_t hash = llvm::xxh3_128bits(llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
		const uint64 halves[2] = { hash.low64, hash.high64 };
		for (size_t i = 0; i < host.buildId.size(); ++i) host.buildId[i] = static_cast<uint8>(halves[i / 8] >> (8 * (i % 8)));
		std::memcpy(bytes.data() + kBuildIdOffset, host.buildId.data(), host.buildId.size());
		return bytes;
	}

	bool WriteInterface(const HostInterface& host, const std::string& path, std::string* error)
	{
		return WriteInterfaceBytes(SerializeInterface(host), path, error);
	}

	// Through FileIo, so an interface in a deep folder is written and read (BUG-356).
	bool WriteInterfaceBytes(const std::string& bytes, const std::string& path, std::string* error)
	{
		if (!WriteFileBytes(path, bytes)) return Fail(error, "cannot write " + path);
		return true;
	}

	bool WriteSealedInterface(const std::string& bytes, const std::string& path, std::string* error)
	{
		const size_t head = kBuildIdOffset + std::tuple_size_v<decltype(HostInterface::buildId)>;
		const std::optional<FileStamp> kept = StampOf(path);
		if (kept && kept->size == bytes.size() && bytes.size() >= head)
			if (auto read = llvm::MemoryBuffer::getFileSlice(path, head, 0); read && (*read)->getBuffer() == llvm::StringRef(bytes.data(), head))
				return true;
		return WriteInterfaceBytes(bytes, path, error);
	}

	bool ReadInterface(const std::string& path, HostInterface& host, std::string* error)
	{
		if (!IsFile(path)) return Fail(error, "cannot read " + path);
		const std::string bytes = ReadFileBytes(path);
		std::string why;
		if (!DeserializeInterface(bytes, host, &why)) return Fail(error, path + ": " + why);
		return true;
	}
} // namespace Scripting
