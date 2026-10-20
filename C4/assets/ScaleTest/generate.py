"""Writes the scale-test program: MODULES modules of about 270 lines each (a record type, a ledger class over
Array and HashMap, a template, a GC-allocated holder, a dozen arithmetic steps, and a Work function that calls the
module before it), and a main that runs every module's Work on several threads at once for a while, checking each
result against the one computed on a single thread first.

    python generate.py <output folder> [modules]
"""
import os
import sys

DEFAULT_MODULES = 200
STEPS = 12


def header(i):
    dep = f'include "m{i - 1}.ch"\n' if i > 0 else ''
    return f'''// Module {i} of the scale test (assets/ScaleTest/generate.py writes it).
include "bedrock.ch"
{dep}
namespace M{i}
{{
	struct Record{i}
	{{
		public int64 id;
		public uint64 value;
		public Core::String label;
	}}

	class Holder{i}
	{{
		public uint64 value;
		public Holder{i}* next;
	}}

	class Ledger{i}
	{{
		[operator("create")] public void Init(uint64 seed) {{ this.seed = seed; }}

		public void Add(uint64 value);
		public uint64 Total() const;
		public Core::String Describe() const;
		public int64 Count() const {{ return records.Count(); }}
		public bool Has(Core::StringView label) const;

		private Core::Array<Record{i}> records;
		private Core::HashMap<Core::String, int64> index;
		private uint64 seed;
	}}

	template<T> T Mix{i}(T a, T b) {{ return (a ^ b) * (T){2 * i + 1} + (T){i}; }}

{chr(10).join(f"	uint64 Step{i}_{k}(uint64 x);" for k in range(STEPS))}
	uint64 Work{i}(uint64 seed);
}}
'''


def step(i, k):
    mul = 6364136223846793005 + 2 * (i * STEPS + k)
    add = 1442695040888963407 + i * 977 + k * 131
    rounds = 3 + (i + k) % 5
    return f'''	uint64 Step{i}_{k}(uint64 x) {{
		uint64 acc = x ^ (uint64){add}u;
		for (int32 r = 0; r < {rounds}; ++r) {{
			acc = acc * (uint64){mul}u + (uint64)r;
			if ((acc & 7u) == (uint64){k % 8}u) acc = acc ^ (acc >> 13);
			else acc = acc + (uint64){(i * 7 + k) % 1000 + 1}u;
		}}
		switch ((int32)(acc % 3u)) {{
			case 0: acc = acc ^ (uint64){i + 1}u; break;
			case 1: acc = acc + (acc >> 7); break;
			default: acc = acc * 3u; break;
		}}
		return acc;
	}}
'''


def source(i):
    steps = '\n'.join(step(i, k) for k in range(STEPS))
    adds = '\n'.join(f'		ledger.Add(Step{i}_{k}(seed + {k}u));' for k in range(STEPS))
    recurse = f'''		if (seed > 64u) sum = sum + M{i - 1}::Work{i - 1}(seed / 2u);
''' if i > 0 else ''
    return f'''include "m{i}.ch"

namespace M{i}
{{
	void Ledger{i}::Add(uint64 value) {{
		Record{i} record;
		record.id = records.Count();
		record.value = value;
		Core::StringBuilder label;
		label.Append("m{i}-");
		label.AppendUInt(value % 1000u);
		record.label = label.ToString();
		index.Insert(record.label, record.id);
		records.Push(take(record));
	}}

	uint64 Ledger{i}::Total() const {{
		uint64 total = seed;
		for (const Record{i}& record : records) total = (total * 1099511628211u) ^ record.value;
		return total;
	}}

	Core::String Ledger{i}::Describe() const {{
		Core::StringBuilder text;
		for (const Record{i}& record : records) {{
			text.Append(record.label.View());
			text.Append(";");
		}}
		return text.ToString();
	}}

	bool Ledger{i}::Has(Core::StringView label) const {{ return index.Contains(Core::String(label)); }}

{steps}
	// Every step into a ledger, its text, two instances of the template, a GC chain, and the module before.
	uint64 Work{i}(uint64 seed) {{
		Ledger{i} ledger = Ledger{i}(seed);
{adds}
		uint64 sum = ledger.Total();
		Core::String text = ledger.Describe();
		sum = sum + (uint64)text.ByteLength();
		sum = Mix{i}<uint64>(sum, (uint64)ledger.Count());
		sum = sum + (uint64)Mix{i}<uint32>((uint32)sum, (uint32){i});
		if (ledger.Has(Core::StringView("m{i}-0"))) sum = sum ^ 1u;
		Holder{i}* chain = nullptr;
		for (int32 k = 0; k < 4; ++k) {{
			Holder{i}* held = new Holder{i}();
			held->value = sum + (uint64)k;
			held->next = chain;
			chain = held;
		}}
		for (Holder{i}* held = chain; held != nullptr; held = held->next) sum = sum ^ (held->value >> 3);
{recurse}		return sum;
	}}
}}
'''


def main_source(modules):
    includes = '\n'.join(f'include "m{i}.ch"' for i in range(modules))
    calls = '\n'.join(f'	sum = sum * 31u + M{i}::Work{i}(seed);' for i in range(modules))
    return f'''// The scale test's entry: every module's Work on SCALE_THREADS threads (default 4) for SCALE_SECONDS seconds
// (default 5), each result checked against the one a single thread computed first. Exit code: the wrong results.
include "bedrock.ch"
{includes}

static uint64 RunAll(uint64 seed) {{
	uint64 sum = 0u;
{calls}
	return sum;
}}

static int64 Setting(const char* name, int64 fallback) {{
	Core::String text = Platform::GetEnvironmentVariable(name);
	if (text.IsEmpty()) return fallback;
	int64 value = 0;
	for (int64 i = 0; i < text.ByteLength(); ++i) value = value * 10 + (int64)(text.View().ByteAt(i) - '0');
	return value;
}}

const int32 kSeeds = 8;
const uint64 kFirstSeed = 12345u;

int32 main() {{
	const int64 seconds = Setting("SCALE_SECONDS", 5);
	const int64 threads = Setting("SCALE_THREADS", 4);
	uint64[8] expected;
	for (int32 s = 0; s < kSeeds; ++s) expected[s] = RunAll(kFirstSeed + (uint64)s);
	Threading::Atomic<int64> runs;
	Threading::Atomic<int64> wrong;
	Threading::Atomic<int64>* runCount = &runs;
	Threading::Atomic<int64>* wrongCount = &wrong;
	uint64* answers = &expected[0];
	const int64 start = clock_now_ns();
	const int64 deadline = start + seconds * 1000000000;
	Core::Array<Threading::Thread> workers;
	for (int64 t = 0; t < threads; ++t)
		workers.Push(Threading::Thread::Run([runCount, wrongCount, answers, deadline, t]() {{
			int64 i = t;
			while (clock_now_ns() < deadline) {{
				const int32 s = (int32)(i % 8);
				if (RunAll(kFirstSeed + (uint64)s) != answers[s]) wrongCount->FetchAdd(1);
				runCount->FetchAdd(1);
				i += 1;
			}}
		}}));
	for (int64 t = 0; t < threads; ++t) workers[t].Join();
	const int64 elapsedMs = (clock_now_ns() - start) / 1000000;
	printf("scale: %lld runs of %d modules on %lld threads in %lld ms, %lld wrong\\n", runs.Load(), {modules}, threads, elapsedMs, wrong.Load());
	return (int32)wrong.Load();
}}
'''


def write(path, text):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def generate(folder, modules):
    os.makedirs(folder, exist_ok=True)
    for name in os.listdir(folder):
        if name.endswith('.cf') or name.endswith('.ch'):
            os.remove(os.path.join(folder, name))
    lines = 0
    for i in range(modules):
        for name, text in ((f'm{i}.ch', header(i)), (f'm{i}.cf', source(i))):
            write(os.path.join(folder, name), text)
            lines += text.count('\n')
    text = main_source(modules)
    write(os.path.join(folder, 'main.cf'), text)
    return lines + text.count('\n')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    count = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_MODULES
    print(f'{generate(sys.argv[1], count)} lines in {2 * count + 1} files')
