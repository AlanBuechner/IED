// The C++ standard library side of the Bedrock benchmark (bedrock.cf): the same kernels over the same data, with the
// same checksums.
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

constexpr int64_t kIntKeys = 1000000;
constexpr int64_t kTextKeys = 200000;
constexpr int64_t kPushes = 10000000;
constexpr int64_t kSorted = 2000000;
constexpr int64_t kFormatted = 1000000;

struct Random
{
	uint64_t state;
	uint64_t Next()
	{
		state += 11400714819323198485ull;
		uint64_t z = state;
		z = (z ^ (z >> 30)) * 13787848793156543929ull;
		z = (z ^ (z >> 27)) * 10723151780598845931ull;
		return z ^ (z >> 31);
	}
};

struct Clock
{
	LARGE_INTEGER start, frequency;
	Clock() { QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start); }
	double Milliseconds() const
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return double(now.QuadPart - start.QuadPart) * 1000.0 / double(frequency.QuadPart);
	}
};

void Report(const char* kernel, const Clock& clock, uint64_t checksum) { std::printf("%s %.3f %llu\n", kernel, clock.Milliseconds(), (unsigned long long)checksum); }

std::vector<int64_t> IntKeys(uint64_t seed, int64_t count)
{
	Random random{ seed };
	std::vector<int64_t> keys;
	keys.reserve(count);
	for (int64_t i = 0; i < count; ++i) keys.push_back(int64_t(random.Next() >> 1));
	return keys;
}

std::vector<std::string> TextKeys(int64_t count)
{
	std::vector<std::string> keys;
	keys.reserve(count);
	for (int64_t i = 0; i < count; ++i) keys.push_back("entity/" + std::to_string(i * 7919) + "/component");
	return keys;
}

void IntegerMap()
{
	std::vector<int64_t> keys = IntKeys(1, kIntKeys);
	std::vector<int64_t> absent = IntKeys(2, kIntKeys);
	std::unordered_map<int64_t, int64_t> map;
	Clock insert;
	for (int64_t i = 0; i < kIntKeys; ++i) map.insert_or_assign(keys[i], i);
	Report("map_int_insert", insert, map.size());
	Clock hit;
	uint64_t sum = 0;
	for (int64_t i = 0; i < kIntKeys; ++i) sum += uint64_t(map.find(keys[i])->second);
	Report("map_int_find_hit", hit, sum);
	Clock miss;
	uint64_t found = 0;
	for (int64_t i = 0; i < kIntKeys; ++i) found += map.find(absent[i]) != map.end() ? 1 : 0;
	Report("map_int_find_miss", miss, found);
	Clock remove;
	uint64_t removed = 0;
	for (int64_t i = 0; i < kIntKeys; i += 2) removed += map.erase(keys[i]);
	Report("map_int_remove", remove, removed + map.size());
}

void TextMap()
{
	std::vector<std::string> keys = TextKeys(kTextKeys);
	std::unordered_map<std::string, int64_t> map;
	Clock insert;
	for (int64_t i = 0; i < kTextKeys; ++i) map.insert_or_assign(keys[i], i);
	Report("map_text_insert", insert, map.size());
	Clock hit;
	uint64_t sum = 0;
	for (int64_t repeat = 0; repeat < 5; ++repeat)
		for (int64_t i = 0; i < kTextKeys; ++i) sum += uint64_t(map.find(keys[i])->second);
	Report("map_text_find", hit, sum);
}

void ArrayPush()
{
	Clock clock;
	std::vector<int32_t> values;
	for (int64_t i = 0; i < kPushes; ++i) values.push_back(int32_t(i * 3));
	uint64_t sum = 0;
	for (int32_t value : values) sum += uint64_t(value);
	Report("array_push_sum", clock, sum);
}

void SortIntegers()
{
	Random random{ 3 };
	std::vector<int32_t> values;
	values.reserve(kSorted);
	for (int64_t i = 0; i < kSorted; ++i) values.push_back(int32_t(random.Next() >> 33));
	Clock clock;
	std::sort(values.begin(), values.end());
	Report("sort_int32", clock, uint64_t(values[0]) + uint64_t(values[kSorted / 2]) * 3 + uint64_t(values[kSorted - 1]) * 7);
}

struct Record
{
	int32_t key;
	int32_t id;
};

void SortRecords()
{
	Random random{ 5 };
	std::vector<Record> records;
	records.reserve(kSorted);
	for (int64_t i = 0; i < kSorted; ++i) records.push_back(Record{ int32_t(random.Next() >> 33), int32_t(i) });
	Clock clock;
	std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) { return a.key < b.key; });
	Report("sort_records", clock, uint64_t(records[0].key) + uint64_t(records[kSorted / 2].key) * 3 + uint64_t(records[kSorted - 1].key) * 7);
}

void BuildText()
{
	Clock clock;
	std::string text;
	char digits[24];
	for (int64_t i = 0; i < kFormatted; ++i)
	{
		const std::to_chars_result written = std::to_chars(digits, digits + sizeof(digits), i * 1000003 - 500000000);
		text.append(digits, written.ptr);
		text.push_back(',');
	}
	Report("text_append_int", clock, text.size());
}

std::string Doubles(uint64_t seed)
{
	Random random{ seed };
	std::string text;
	char digits[64];
	for (int64_t i = 0; i < kFormatted; ++i)
	{
		const double value = double(random.Next() >> 11) / 9007199254740992.0 * 1000.0;
		const std::to_chars_result written = std::to_chars(digits, digits + sizeof(digits), value, std::chars_format::fixed);
		text.append(digits, written.ptr);
		text.push_back(',');
	}
	return text;
}

void FormatDoubles()
{
	Clock clock;
	std::string text = Doubles(4);
	Report("text_format_double", clock, text.size());
}

void ParseDoubles()
{
	std::string text = Doubles(5);
	Clock clock;
	const char* at = text.data();
	const char* end = at + text.size();
	double sum = 0.0;
	while (at < end)
	{
		double value = 0.0;
		const std::from_chars_result read = std::from_chars(at, end, value);
		sum += value;
		at = read.ptr + 1;
	}
	Report("text_parse_double", clock, uint64_t(sum));
}

int main()
{
	IntegerMap();
	TextMap();
	ArrayPush();
	SortIntegers();
	SortRecords();
	BuildText();
	FormatDoubles();
	ParseDoubles();
	return 0;
}
