// Source lines from DWARF line tables (.debug_line, versions 2 to 5: ModulesLinux.h). A table is a program per
// compilation unit whose rows map code addresses to a file and line; the address range from one row to the next in a
// sequence is the first row's. c4_linux_elf_lines answers a few addresses in one pass over every program (a crash
// report's frames, async-signal-safe); an index answers many (SourceLineOf, a profiler's frames).

#include "ModulesLinux.h"
#include <stdlib.h>
#include <string.h>

// Standard opcodes (DWARF 5, 6.2.5.2) and extended ones (6.2.5.3) the rows depend on; the others are skipped by the
// operand counts the header gives.
enum { kCopy = 1, kAdvancePc = 2, kAdvanceLine = 3, kSetFile = 4, kConstAddPc = 8, kFixedAdvancePc = 9 };
enum { kExtended = 0, kEndSequence = 1, kSetAddress = 2 };
enum { kHighestSpecialOpcode = 255 };
// The content of a directory or file entry (DWARF 5, 6.2.4.1) and the forms that write it (7.5.6).
enum { kContentPath = 1, kContentDirectoryIndex = 2 };
enum
{
	kFormBlock2 = 0x03, kFormBlock4 = 0x04, kFormData2 = 0x05, kFormData4 = 0x06, kFormData8 = 0x07, kFormString = 0x08,
	kFormBlock = 0x09, kFormBlock1 = 0x0a, kFormData1 = 0x0b, kFormStrp = 0x0e, kFormUdata = 0x0f, kFormStrx = 0x1a,
	kFormData16 = 0x1e, kFormLineStrp = 0x1f, kFormStrx1 = 0x25, kFormStrx2 = 0x26, kFormStrx3 = 0x27, kFormStrx4 = 0x28
};
// A unit length of this says the unit is 64-bit DWARF: the real length follows in 8 bytes, and offsets are 8 bytes.
static const uint64_t kWideUnit = 0xffffffffu;
// An index keeps a sequence's first row and every this many rows after it.
enum { kCheckpointStride = 32 };

// --- reading -----------------------------------------------------------------------------------------------

typedef struct Reader
{
	const uint8_t* at;
	const uint8_t* end;
	bool failed;
} Reader;

static uint64_t ReadFixed(Reader* reader, uint32_t bytes)
{
	if ((uint64_t)(reader->end - reader->at) < bytes)
	{
		reader->failed = true;
		reader->at = reader->end;
		return 0;
	}
	uint64_t value = 0;
	for (uint32_t i = 0; i < bytes; ++i) value |= (uint64_t)reader->at[i] << (8 * i);
	reader->at += bytes;
	return value;
}

static void Skip(Reader* reader, uint64_t bytes)
{
	if ((uint64_t)(reader->end - reader->at) < bytes)
	{
		reader->failed = true;
		reader->at = reader->end;
	}
	else reader->at += bytes;
}

static uint64_t ReadUleb(Reader* reader)
{
	uint64_t value = 0;
	for (uint32_t shift = 0; reader->at < reader->end; shift += 7)
	{
		const uint8_t byte = *reader->at++;
		if (shift < 64) value |= (uint64_t)(byte & 0x7f) << shift;
		if (!(byte & 0x80)) return value;
	}
	reader->failed = true;
	return value;
}

static int64_t ReadSleb(Reader* reader)
{
	uint64_t value = 0;
	uint32_t shift = 0;
	while (reader->at < reader->end)
	{
		const uint8_t byte = *reader->at++;
		if (shift < 64) value |= (uint64_t)(byte & 0x7f) << shift;
		shift += 7;
		if (byte & 0x80) continue;
		if (shift < 64 && (byte & 0x40)) value |= ~(uint64_t)0 << shift;
		return (int64_t)value;
	}
	reader->failed = true;
	return 0;
}

static const char* ReadString(Reader* reader)
{
	const uint8_t* zero = (const uint8_t*)memchr(reader->at, 0, (size_t)(reader->end - reader->at));
	if (!zero)
	{
		reader->failed = true;
		reader->at = reader->end;
		return "";
	}
	const char* text = (const char*)reader->at;
	reader->at = zero + 1;
	return text;
}

// A string section (.debug_line_str, .debug_str) and the string at an offset in it; null when there is none.
typedef struct Strings
{
	const uint8_t* bytes;
	uint64_t size;
} Strings;

static const char* StringAt(Strings strings, uint64_t offset)
{
	if (offset >= strings.size || !memchr(strings.bytes + offset, 0, (size_t)(strings.size - offset))) return NULL;
	return (const char*)strings.bytes + offset;
}

typedef struct Sections
{
	const uint8_t* line;
	uint64_t lineSize;
	Strings lineStrings;
	Strings strings;
} Sections;

static bool FindSections(const c4_linux_elf* elf, Sections* sections)
{
	memset(sections, 0, sizeof(*sections));
	c4_linux_elf_section(elf, ".debug_line_str", &sections->lineStrings.bytes, &sections->lineStrings.size);
	c4_linux_elf_section(elf, ".debug_str", &sections->strings.bytes, &sections->strings.size);
	return c4_linux_elf_section(elf, ".debug_line", &sections->line, &sections->lineSize);
}

// --- a unit's header ---------------------------------------------------------------------------------------

typedef struct LineUnit
{
	const Sections* sections;
	uint16_t version;
	bool wide;                       // 64-bit DWARF
	uint8_t minimumInstructionLength;
	int8_t lineBase;
	uint8_t lineRange;
	uint8_t opcodeBase;
	const uint8_t* standardLengths;  // the operand counts of standard opcodes 1 to opcodeBase - 1
	const uint8_t* tables;           // the directories and files
	const uint8_t* program;
	const uint8_t* end;
} LineUnit;

// The header of the unit at `at`, with `*next` the unit after it (null when even its length cannot be read). False when
// the unit cannot be read; its rows are skipped.
static bool ReadUnit(const Sections* sections, const uint8_t* at, LineUnit* unit, const uint8_t** next)
{
	Reader reader = { at, sections->line + sections->lineSize, false };
	uint64_t length = ReadFixed(&reader, 4);
	unit->wide = length == kWideUnit;
	if (unit->wide) length = ReadFixed(&reader, 8);
	*next = NULL;
	if (reader.failed || length > (uint64_t)(reader.end - reader.at)) return false;
	unit->end = reader.at + length;
	*next = unit->end;
	reader.end = unit->end;
	unit->sections = sections;
	unit->version = (uint16_t)ReadFixed(&reader, 2);
	if (unit->version < 2 || unit->version > 5) return false;
	if (unit->version >= 5) Skip(&reader, 2); // the address and segment selector sizes
	const uint64_t headerLength = ReadFixed(&reader, unit->wide ? 8 : 4);
	if (reader.failed || headerLength > (uint64_t)(reader.end - reader.at)) return false;
	unit->program = reader.at + headerLength;
	unit->minimumInstructionLength = (uint8_t)ReadFixed(&reader, 1);
	if (unit->version >= 4) Skip(&reader, 1); // the most operations an instruction holds: 1 but on VLIW machines
	Skip(&reader, 1);                         // default_is_stmt
	unit->lineBase = (int8_t)ReadFixed(&reader, 1);
	unit->lineRange = (uint8_t)ReadFixed(&reader, 1);
	unit->opcodeBase = (uint8_t)ReadFixed(&reader, 1);
	unit->standardLengths = reader.at;
	Skip(&reader, unit->opcodeBase > 0 ? unit->opcodeBase - 1u : 0u);
	unit->tables = reader.at;
	return !reader.failed && unit->lineRange != 0 && unit->opcodeBase != 0 && unit->tables <= unit->program;
}

// --- file names --------------------------------------------------------------------------------------------

// DWARF 2 to 4: the `index`th (from 1) of a list of strings an empty one ends; null when there are fewer.
static const char* NthString(Reader reader, uint64_t index)
{
	for (uint64_t at = 1;; ++at)
	{
		const char* text = ReadString(&reader);
		if (reader.failed || !*text) return NULL;
		if (at == index) return text;
	}
}

static void SkipStrings(Reader* reader)
{
	while (!reader->failed && *ReadString(reader)) {}
}

// "/src/a.cf", or "C:\src\a.cf" and "\\server\a.cf" from a program built on Windows.
static bool IsAbsolute(const char* path)
{
	const bool drive = ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':';
	return path[0] == '/' || path[0] == '\\' || drive;
}

// DWARF 2 to 4: the directories, then the files - a name, its directory's index (0: the unit's own folder, which only
// the unit's debug information names), its time and its size. A file is numbered from 1.
static bool NameFileUpTo4(const LineUnit* unit, uint64_t file, c4_linux_line* line)
{
	Reader reader = { unit->tables, unit->program, false };
	const Reader directories = reader;
	SkipStrings(&reader);
	for (uint64_t at = 1;; ++at)
	{
		const char* name = ReadString(&reader);
		if (reader.failed || !*name) return false;
		const uint64_t directory = ReadUleb(&reader);
		ReadUleb(&reader);
		ReadUleb(&reader);
		if (at != file) continue;
		const char* folder = IsAbsolute(name) || directory == 0 ? "" : NthString(directories, directory);
		line->directory = folder ? folder : "";
		line->name = name;
		return true;
	}
}

// DWARF 5: an entry's fields as its format lists them (pairs of content and form).
typedef struct EntryFormat
{
	const uint8_t* pairs;
	uint8_t count;
} EntryFormat;

static void ReadFormat(Reader* reader, EntryFormat* format)
{
	format->count = (uint8_t)ReadFixed(reader, 1);
	format->pairs = reader->at;
	for (uint8_t i = 0; i < format->count; ++i)
	{
		ReadUleb(reader);
		ReadUleb(reader);
	}
}

// One field: a number into `*number`, a string into `*text` (null when it is in a table this reader does not read).
static bool ReadForm(Reader* reader, const LineUnit* unit, uint64_t form, uint64_t* number, const char** text)
{
	const uint32_t offsetBytes = unit->wide ? 8 : 4;
	*number = 0;
	*text = NULL;
	switch (form)
	{
	case kFormString: *text = ReadString(reader); break;
	case kFormLineStrp: *text = StringAt(unit->sections->lineStrings, ReadFixed(reader, offsetBytes)); break;
	case kFormStrp: *text = StringAt(unit->sections->strings, ReadFixed(reader, offsetBytes)); break;
	case kFormUdata: *number = ReadUleb(reader); break;
	case kFormData1: *number = ReadFixed(reader, 1); break;
	case kFormData2: *number = ReadFixed(reader, 2); break;
	case kFormData4: *number = ReadFixed(reader, 4); break;
	case kFormData8: *number = ReadFixed(reader, 8); break;
	case kFormData16: Skip(reader, 16); break;
	case kFormBlock: Skip(reader, ReadUleb(reader)); break;
	case kFormBlock1: Skip(reader, ReadFixed(reader, 1)); break;
	case kFormBlock2: Skip(reader, ReadFixed(reader, 2)); break;
	case kFormBlock4: Skip(reader, ReadFixed(reader, 4)); break;
	case kFormStrx: ReadUleb(reader); break;
	case kFormStrx1: Skip(reader, 1); break;
	case kFormStrx2: Skip(reader, 2); break;
	case kFormStrx3: Skip(reader, 3); break;
	case kFormStrx4: Skip(reader, 4); break;
	default: return false;
	}
	return !reader->failed;
}

// A DWARF 5 entry's path and directory index.
static bool ReadEntry(Reader* reader, const LineUnit* unit, const EntryFormat* format, const char** path, uint64_t* directory)
{
	Reader pairs = { format->pairs, unit->program, false };
	*path = NULL;
	*directory = 0;
	for (uint8_t i = 0; i < format->count; ++i)
	{
		const uint64_t content = ReadUleb(&pairs);
		const uint64_t form = ReadUleb(&pairs);
		uint64_t number = 0;
		const char* text = NULL;
		if (pairs.failed || !ReadForm(reader, unit, form, &number, &text)) return false;
		if (content == kContentPath) *path = text;
		else if (content == kContentDirectoryIndex) *directory = number;
	}
	return true;
}

// The `index`th (from 0) entry of a DWARF 5 list; the reader ends after the list when `index` is past it.
static bool NthEntry(Reader* reader, const LineUnit* unit, uint64_t index, const char** path, uint64_t* directory)
{
	EntryFormat format;
	ReadFormat(reader, &format);
	const uint64_t count = ReadUleb(reader);
	bool found = false;
	for (uint64_t at = 0; at < count && !reader->failed; ++at)
	{
		const char* entryPath = NULL;
		uint64_t entryDirectory = 0;
		if (!ReadEntry(reader, unit, &format, &entryPath, &entryDirectory))
		{
			reader->failed = true;
			return false;
		}
		if (at != index) continue;
		*path = entryPath;
		*directory = entryDirectory;
		found = true;
	}
	return found && !reader->failed;
}

// DWARF 5: the directories (the first the unit's own folder), then the files, numbered from 0, each as its format says.
static bool NameFile5(const LineUnit* unit, uint64_t file, c4_linux_line* line)
{
	Reader reader = { unit->tables, unit->program, false };
	const Reader directories = reader;
	const char* ignored = NULL;
	uint64_t none = 0;
	NthEntry(&reader, unit, UINT64_MAX, &ignored, &none);
	const char* name = NULL;
	uint64_t directory = 0;
	if (reader.failed || !NthEntry(&reader, unit, file, &name, &directory) || !name) return false;
	const char* folder = NULL;
	Reader again = directories;
	if (!IsAbsolute(name) && !NthEntry(&again, unit, directory, &folder, &none)) folder = NULL;
	line->directory = folder ? folder : "";
	line->name = name;
	return true;
}

static bool NameFile(const LineUnit* unit, uint64_t file, c4_linux_line* line)
{
	return unit->version >= 5 ? NameFile5(unit, file, line) : NameFileUpTo4(unit, file, line);
}

// The separator a directory's own path uses last ('\' in one from Windows, where clang mixes them), none when it ends
// with one.
static const char* SeparatorAfter(const char* directory)
{
	const size_t length = strlen(directory);
	if (length == 0 || directory[length - 1] == '/' || directory[length - 1] == '\\') return "";
	const char* slash = strrchr(directory, '/');
	const char* backslash = strrchr(directory, '\\');
	return backslash > slash ? "\\" : "/";
}

int32_t c4_linux_line_path(const c4_linux_line* line, char* out, int32_t capacity)
{
	const char* directory = line->directory && !IsAbsolute(line->name) ? line->directory : "";
	const char* separator = SeparatorAfter(directory);
	const size_t lengths[3] = { strlen(directory), strlen(separator), strlen(line->name) };
	const size_t length = lengths[0] + lengths[1] + lengths[2];
	if (capacity <= 0 || length >= (size_t)capacity) return -1;
	memcpy(out, directory, lengths[0]);
	memcpy(out + lengths[0], separator, lengths[1]);
	memcpy(out + lengths[0] + lengths[1], line->name, lengths[2] + 1);
	return (int32_t)length;
}

// --- running a program -------------------------------------------------------------------------------------

// The registers a row is made of that a file and line depend on.
typedef struct Row
{
	uint64_t address;
	uint64_t file;
	int64_t line;
} Row;

static const Row kSequenceStart = { 0, 1, 1 };

// Called for each row a program makes: `previous` is the row before it in its sequence (null for a sequence's first),
// `after` where the program goes on, `ends` whether it ends its sequence (its address is the sequence's end). False
// stops the run.
typedef bool (*RowVisitor)(const LineUnit* unit, const Row* previous, const Row* row, const uint8_t* after, bool ends, void* state);

static void RunStandard(const LineUnit* unit, uint8_t opcode, Reader* reader, Row* registers, bool* emits)
{
	switch (opcode)
	{
	case kCopy: *emits = true; break;
	case kAdvancePc: registers->address += ReadUleb(reader) * unit->minimumInstructionLength; break;
	case kAdvanceLine: registers->line += ReadSleb(reader); break;
	case kSetFile: registers->file = ReadUleb(reader); break;
	case kConstAddPc:
		registers->address += (uint64_t)((kHighestSpecialOpcode - unit->opcodeBase) / unit->lineRange) * unit->minimumInstructionLength;
		break;
	case kFixedAdvancePc: registers->address += ReadFixed(reader, 2); break;
	default:
		for (uint8_t i = 0; i < unit->standardLengths[opcode - 1]; ++i) ReadUleb(reader);
		break;
	}
}

static void RunExtended(Reader* reader, Row* registers, bool* emits, bool* ends)
{
	const uint64_t length = ReadUleb(reader);
	if (reader->failed || length == 0 || length > (uint64_t)(reader->end - reader->at))
	{
		reader->failed = true;
		return;
	}
	const uint8_t* next = reader->at + length;
	const uint8_t opcode = (uint8_t)ReadFixed(reader, 1);
	if (opcode == kEndSequence) *emits = *ends = true;
	else if (opcode == kSetAddress && length - 1 <= sizeof(uint64_t)) registers->address = ReadFixed(reader, (uint32_t)(length - 1));
	reader->at = next;
}

static void RunSpecial(const LineUnit* unit, uint8_t opcode, Row* registers)
{
	const uint8_t adjusted = (uint8_t)(opcode - unit->opcodeBase);
	registers->address += (uint64_t)(adjusted / unit->lineRange) * unit->minimumInstructionLength;
	registers->line += unit->lineBase + adjusted % unit->lineRange;
}

// Runs the program from `at` with `registers`, which are a row already made when `made`. False when the visitor stopped.
static bool RunProgram(const LineUnit* unit, const uint8_t* at, Row registers, bool made, RowVisitor visit, void* state)
{
	Reader reader = { at, unit->end, false };
	Row previous = registers;
	while (reader.at < reader.end && !reader.failed)
	{
		const uint8_t opcode = (uint8_t)ReadFixed(&reader, 1);
		bool emits = false;
		bool ends = false;
		if (opcode >= unit->opcodeBase)
		{
			RunSpecial(unit, opcode, &registers);
			emits = true;
		}
		else if (opcode == kExtended) RunExtended(&reader, &registers, &emits, &ends);
		else RunStandard(unit, opcode, &reader, &registers, &emits);
		if (!emits || reader.failed) continue;
		if (!visit(unit, made ? &previous : NULL, &registers, reader.at, ends, state)) return false;
		previous = registers;
		made = !ends;
		if (ends) registers = kSequenceStart;
	}
	return true;
}

// Runs every unit's program. False when the visitor stopped.
static bool RunUnits(const Sections* sections, RowVisitor visit, void* state)
{
	for (const uint8_t* at = sections->line; at && at < sections->line + sections->lineSize;)
	{
		LineUnit unit;
		const uint8_t* next = NULL;
		if (ReadUnit(sections, at, &unit, &next) && !RunProgram(&unit, unit.program, kSequenceStart, false, visit, state)) return false;
		at = next;
	}
	return true;
}

// --- a few addresses in one pass ---------------------------------------------------------------------------

typedef struct Questions
{
	c4_linux_line* lines;
	int32_t count;
	int32_t open;
} Questions;

// The first of `lines` (sorted) at or after `address`.
static int32_t FirstFrom(const c4_linux_line* lines, int32_t count, uint64_t address)
{
	int32_t low = 0;
	int32_t high = count;
	while (low < high)
	{
		const int32_t middle = low + (high - low) / 2;
		if (lines[middle].address < address) low = middle + 1;
		else high = middle;
	}
	return low;
}

// `row`'s file and line answer `line`, unless it is answered already.
static bool Answer(const LineUnit* unit, const Row* row, c4_linux_line* line)
{
	if (line->name || !NameFile(unit, row->file, line)) return false;
	line->line = (int32_t)row->line;
	return true;
}

static void Unanswered(c4_linux_line* line)
{
	line->directory = NULL;
	line->name = NULL;
	line->line = 0;
}

// A row's range [previous, row) is previous's: it answers the addresses in it. A row of line 0 is code no line wrote.
static bool AnswerQuestions(const LineUnit* unit, const Row* previous, const Row* row, const uint8_t* after, bool ends, void* state)
{
	(void)after;
	(void)ends;
	Questions* questions = (Questions*)state;
	if (!previous || previous->line <= 0 || row->address <= previous->address) return true;
	for (int32_t i = FirstFrom(questions->lines, questions->count, previous->address); i < questions->count && questions->lines[i].address < row->address; ++i)
		if (Answer(unit, previous, &questions->lines[i])) questions->open--;
	return questions->open > 0;
}

void c4_linux_elf_lines(const c4_linux_elf* elf, c4_linux_line* lines, int32_t count)
{
	for (int32_t i = 0; i < count; ++i) Unanswered(&lines[i]);
	Sections sections;
	if (count <= 0 || !FindSections(elf, &sections)) return;
	Questions questions = { lines, count, count };
	RunUnits(&sections, AnswerQuestions, &questions);
}

// --- an index ----------------------------------------------------------------------------------------------

// A row and where its program goes on, its unit and its continuation as offsets in .debug_line.
typedef struct c4_linux_line_checkpoint
{
	uint64_t address;
	uint64_t file;
	int64_t line;
	uint64_t unit;
	uint64_t after;
} c4_linux_line_checkpoint;

typedef struct Builder
{
	const Sections* sections;
	const uint8_t* unit;           // the unit whose program runs
	c4_linux_line_index* index;
	int64_t capacity;
	int64_t rowsSinceCheckpoint;
	bool outOfMemory;
} Builder;

static bool Keep(Builder* builder, const Row* row, const uint8_t* after)
{
	c4_linux_line_index* index = builder->index;
	if (index->count == builder->capacity)
	{
		const int64_t capacity = builder->capacity ? builder->capacity * 2 : 1024;
		c4_linux_line_checkpoint* grown = (c4_linux_line_checkpoint*)realloc(index->checkpoints, (size_t)capacity * sizeof(*grown));
		if (!grown) return false;
		index->checkpoints = grown;
		builder->capacity = capacity;
	}
	const uint64_t unit = (uint64_t)(builder->unit - builder->sections->line);
	index->checkpoints[index->count++] = (c4_linux_line_checkpoint){ row->address, row->file, row->line, unit, (uint64_t)(after - builder->sections->line) };
	return true;
}

static bool KeepCheckpoints(const LineUnit* unit, const Row* previous, const Row* row, const uint8_t* after, bool ends, void* state)
{
	(void)unit;
	Builder* builder = (Builder*)state;
	if (ends) return true;
	if (previous && ++builder->rowsSinceCheckpoint < kCheckpointStride) return true;
	builder->rowsSinceCheckpoint = 0;
	builder->outOfMemory = !Keep(builder, row, after);
	return !builder->outOfMemory;
}

static int CompareCheckpoints(const void* a, const void* b)
{
	const uint64_t left = ((const c4_linux_line_checkpoint*)a)->address;
	const uint64_t right = ((const c4_linux_line_checkpoint*)b)->address;
	return left < right ? -1 : left > right ? 1 : 0;
}

bool c4_linux_line_index_build(const c4_linux_elf* elf, c4_linux_line_index* index)
{
	memset(index, 0, sizeof(*index));
	Sections sections;
	if (!FindSections(elf, &sections)) return false;
	Builder builder = { &sections, NULL, index, 0, 0, false };
	for (const uint8_t* at = sections.line; at && at < sections.line + sections.lineSize && !builder.outOfMemory;)
	{
		LineUnit unit;
		const uint8_t* next = NULL;
		builder.unit = at;
		if (ReadUnit(&sections, at, &unit, &next)) RunProgram(&unit, unit.program, kSequenceStart, false, KeepCheckpoints, &builder);
		at = next;
	}
	if (builder.outOfMemory || index->count == 0)
	{
		c4_linux_line_index_free(index);
		return false;
	}
	qsort(index->checkpoints, (size_t)index->count, sizeof(c4_linux_line_checkpoint), CompareCheckpoints);
	return true;
}

void c4_linux_line_index_free(c4_linux_line_index* index)
{
	free(index->checkpoints);
	memset(index, 0, sizeof(*index));
}

// The last checkpoint at or before `address`, -1 when none is.
static int64_t CheckpointBefore(const c4_linux_line_index* index, uint64_t address)
{
	int64_t low = 0;
	int64_t high = index->count;
	while (low < high)
	{
		const int64_t middle = low + (high - low) / 2;
		if (index->checkpoints[middle].address <= address) low = middle + 1;
		else high = middle;
	}
	return low - 1;
}

// From a checkpoint the rows run until one passes the address or its sequence ends.
static bool AnswerOne(const LineUnit* unit, const Row* previous, const Row* row, const uint8_t* after, bool ends, void* state)
{
	(void)after;
	c4_linux_line* line = (c4_linux_line*)state;
	if (!previous || row->address <= line->address) return !ends;
	if (previous->line > 0 && previous->address <= line->address) Answer(unit, previous, line);
	return false;
}

bool c4_linux_line_index_find(const c4_linux_elf* elf, const c4_linux_line_index* index, c4_linux_line* line)
{
	Unanswered(line);
	const int64_t at = CheckpointBefore(index, line->address);
	Sections sections;
	if (at < 0 || !FindSections(elf, &sections)) return false;
	const c4_linux_line_checkpoint* checkpoint = &index->checkpoints[at];
	LineUnit unit;
	const uint8_t* next = NULL;
	if (checkpoint->unit >= sections.lineSize || !ReadUnit(&sections, sections.line + checkpoint->unit, &unit, &next)) return false;
	const uint8_t* after = sections.line + checkpoint->after;
	if (after < unit.program || after > unit.end) return false;
	const Row row = { checkpoint->address, checkpoint->file, checkpoint->line };
	RunProgram(&unit, after, row, true, AnswerOne, line);
	return line->name != NULL;
}
