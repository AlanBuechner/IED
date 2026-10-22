include "bedrock.ch"

namespace WordFreq
{
	struct WordCount
	{
		public Core::String word;
		public int64 count;
	}

	// The ASCII-letter words of `text`, lowercased, with how often each occurs.
	Core::HashMap<Core::String, int64> CountWords(Core::StringView text);
	// At most `limit` of them: the most frequent first, equal counts by word.
	Core::Array<WordCount> TopWords(const Core::HashMap<Core::String, int64>& counts, int64 limit);
	Core::String Render(const Core::Array<WordCount>& ranked);
}
