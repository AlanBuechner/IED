"""The wordfreq benchmark: about 60 MB of text whose words follow a Zipf law, in mixed case, with punctuation, digits
and UTF-8; writes <out>/input.txt and <out>/expected (the top 25)."""

import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wordfreq_ref  # noqa: E402

TARGET_BYTES = 60 * 1024 * 1024
VOCABULARY = 50000


def vocabulary(rng):
	letters = "abcdefghijklmnopqrstuvwxyz"
	words = set()
	while len(words) < VOCABULARY:
		words.add("".join(rng.choice(letters) for _ in range(rng.randint(2, 11))))
	return sorted(words)


def main():
	out = sys.argv[1]
	rng = random.Random(4242)
	words = vocabulary(rng)
	weights = [1.0 / (rank + 1) for rank in range(len(words))]
	separators = [" ", " ", " ", " ", ", ", ". ", "\n", " - ", "'", "42 ", "\t", " café ", "; "]
	written = 0
	with open(os.path.join(out, "input.txt"), "w", encoding="utf-8", newline="") as f:
		while written < TARGET_BYTES:
			chunk = []
			for word in rng.choices(words, weights, k=20000):
				form = rng.random()
				word = word.upper() if form < 0.03 else word.capitalize() if form < 0.15 else word
				chunk.append(word + rng.choice(separators))
			text = "".join(chunk)
			f.write(text)
			written += len(text.encode("utf-8"))
	with open(os.path.join(out, "input.txt"), "rb") as f:
		ranked = wordfreq_ref.top_words(f.read(), 25)
	with open(os.path.join(out, "expected"), "w", encoding="utf-8", newline="\n") as f:
		f.write(wordfreq_ref.render(ranked))


if __name__ == "__main__":
	main()
