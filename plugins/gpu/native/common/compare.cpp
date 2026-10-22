// compare.cpp - capture compare (compare.h): the alignment is a longest common subsequence per marker level, after the
// common head and tail are taken off (two builds of one program mostly differ in a few places); a level too long for
// the exact table aligns greedily.
#include "compare.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace fb
{
	namespace
	{
		constexpr size_t kTableCells = 4u << 20;    // the largest LCS table worked out exactly
		constexpr size_t kGreedyLookahead = 256;

		std::string Call(const std::string& name) {
			const size_t open = name.find('(');
			return open == std::string::npos ? name : name.substr(0, open);
		}

		std::string Arguments(const std::string& name) {
			const size_t open = name.find('(');
			return open == std::string::npos ? "" : name.substr(open);
		}

		class Aligner
		{
		public:
			Aligner(const std::vector<CompareEvent>& a, const std::vector<CompareEvent>& b, bool sameApi) : a_(a), b_(b), sameApi_(sameApi) {
				Children(a, childrenA_);
				Children(b, childrenB_);
			}

			std::vector<AlignedEvent> Run() {
				Level(ChildrenOf(childrenA_, 0), ChildrenOf(childrenB_, 0), 0);
				return std::move(out_);
			}

		private:
			using Tree = std::unordered_map<u32, std::vector<size_t>>;    // parent EID -> its visible children, in order
			using Pairs = std::vector<std::pair<size_t, size_t>>;

			static void Children(const std::vector<CompareEvent>& events, Tree& tree) {
				for (size_t i = 0; i < events.size(); ++i)
					if (!events[i].hidden) tree[events[i].parent].push_back(i);
			}

			static const std::vector<size_t>& ChildrenOf(const Tree& tree, u32 eid) {
				static const std::vector<size_t> none;
				auto found = tree.find(eid);
				return found == tree.end() ? none : found->second;
			}

			u32 Key(const CompareEvent& e) {
				const std::string text = e.kind == "marker" ? "marker:" + e.name : sameApi_ ? e.kind + ":" + Call(e.name) : e.kind;
				return keys_.emplace(text, (u32)keys_.size()).first->second;
			}

			bool Changed(const CompareEvent& a, const CompareEvent& b) const {
				if (sameApi_ || a.kind == "marker") return WithoutObjectNumbers(a.name) != WithoutObjectNumbers(b.name);
				return WithoutObjectNumbers(Arguments(a.name)) != WithoutObjectNumbers(Arguments(b.name));
			}

			Pairs Match(const std::vector<size_t>& a, const std::vector<size_t>& b) {
				std::vector<u32> ka, kb;
				for (size_t i : a) ka.push_back(Key(a_[i]));
				for (size_t j : b) kb.push_back(Key(b_[j]));
				Pairs pairs;
				size_t head = 0, tail = 0;
				while (head < ka.size() && head < kb.size() && ka[head] == kb[head]) pairs.push_back({head, head}), ++head;
				while (tail < ka.size() - head && tail < kb.size() - head && ka[ka.size() - 1 - tail] == kb[kb.size() - 1 - tail]) ++tail;
				const size_t n = ka.size() - head - tail, m = kb.size() - head - tail;
				if (n && m) {
					if (n * m <= kTableCells) Exact(ka.data() + head, kb.data() + head, n, m, head, pairs);
					else Greedy(ka.data() + head, kb.data() + head, n, m, head, pairs);
				}
				for (size_t k = tail; k > 0; --k) pairs.push_back({ka.size() - k, kb.size() - k});
				return pairs;
			}

			static void Exact(const u32* ka, const u32* kb, size_t n, size_t m, size_t offset, Pairs& pairs) {
				// length(i, j): the longest common subsequence of ka[i..] and kb[j..].
				std::vector<u32> length((n + 1) * (m + 1), 0);
				auto at = [&](size_t i, size_t j) -> u32& { return length[i * (m + 1) + j]; };
				for (size_t i = n; i-- > 0;)
					for (size_t j = m; j-- > 0;) at(i, j) = ka[i] == kb[j] ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
				for (size_t i = 0, j = 0; i < n && j < m;) {
					if (ka[i] == kb[j]) pairs.push_back({offset + i++, offset + j++});
					else if (at(i + 1, j) >= at(i, j + 1)) ++i;
					else ++j;
				}
			}

			static void Greedy(const u32* ka, const u32* kb, size_t n, size_t m, size_t offset, Pairs& pairs) {
				size_t j = 0;
				for (size_t i = 0; i < n && j < m; ++i)
					for (size_t k = j; k < m && k < j + kGreedyLookahead; ++k)
						if (ka[i] == kb[k]) {
							pairs.push_back({offset + i, offset + k});
							j = k + 1;
							break;
						}
			}

			void Level(const std::vector<size_t>& a, const std::vector<size_t>& b, u32 depth) {
				size_t i = 0, j = 0;
				for (const auto& pair : Match(a, b)) {
					while (i < pair.first) Only(a_, childrenA_, a[i++], depth, true);
					while (j < pair.second) Only(b_, childrenB_, b[j++], depth, false);
					const CompareEvent& ea = a_[a[i++]];
					const CompareEvent& eb = b_[b[j++]];
					AlignedEvent row;
					row.a = ea.eid;
					row.b = eb.eid;
					row.depth = depth;
					row.changed = Changed(ea, eb);
					out_.push_back(row);
					if (ea.kind == "marker") Level(ChildrenOf(childrenA_, ea.eid), ChildrenOf(childrenB_, eb.eid), depth + 1);
				}
				while (i < a.size()) Only(a_, childrenA_, a[i++], depth, true);
				while (j < b.size()) Only(b_, childrenB_, b[j++], depth, false);
			}

			void Only(const std::vector<CompareEvent>& events, const Tree& tree, size_t index, u32 depth, bool first) {
				AlignedEvent row;
				(first ? row.a : row.b) = events[index].eid;
				row.depth = depth;
				out_.push_back(row);
				for (size_t child : ChildrenOf(tree, events[index].eid)) Only(events, tree, child, depth + 1, first);
			}

			const std::vector<CompareEvent>& a_;
			const std::vector<CompareEvent>& b_;
			bool sameApi_;
			Tree childrenA_, childrenB_;
			std::unordered_map<std::string, u32> keys_;
			std::vector<AlignedEvent> out_;
		};

		using Labeled = std::vector<std::pair<std::string, const JsonValue*>>;

		// A list's entries by their label, a repeated label told apart by its order ("Blend 0", "Blend 0#2").
		Labeled ByLabel(const JsonValue& list, const char* field) {
			Labeled out;
			std::map<std::string, u32> seen;
			for (size_t i = 0; i < list.Count(); ++i) {
				const JsonValue& entry = list.At(i);
				const std::string label = entry[field].Str();
				const u32 n = ++seen[label];
				out.push_back({n == 1 ? label : label + "#" + std::to_string(n), &entry});
			}
			return out;
		}

		// Two lists of a state answer: its groups (by name, holding items) or items (by label, with a value and children).
		void DiffLists(const JsonValue& a, const JsonValue& b, bool groups, const std::string& path, std::vector<StateDifference>& out, u32& compared) {
			const char* field = groups ? "name" : "label";
			const char* inner = groups ? "items" : "children";
			std::vector<std::string> order;
			std::map<std::string, std::pair<const JsonValue*, const JsonValue*>> both;
			for (const auto& entry : ByLabel(a, field)) {
				order.push_back(entry.first);
				both[entry.first].first = entry.second;
			}
			for (const auto& entry : ByLabel(b, field)) {
				if (!both.count(entry.first)) order.push_back(entry.first);
				both[entry.first].second = entry.second;
			}
			for (const std::string& label : order) {
				const JsonValue* ea = both[label].first;
				const JsonValue* eb = both[label].second;
				const std::string here = path.empty() ? label : path + " / " + label;
				if (!groups) {
					++compared;
					const std::string va = ea ? (*ea)["value"].Str() : "", vb = eb ? (*eb)["value"].Str() : "";
					if (!ea || !eb || WithoutObjectNumbers(va) != WithoutObjectNumbers(vb)) out.push_back({here, ea ? va : "", eb ? vb : ""});
				}
				if (ea && eb) DiffLists((*ea)[inner], (*eb)[inner], false, here, out, compared);
				else if (groups) out.push_back({here, ea ? "present" : "", eb ? "present" : ""});
			}
		}
	}

	std::string WithoutObjectNumbers(const std::string& text) {
		std::string out;
		for (size_t i = 0; i < text.size(); ++i) {
			out += text[i];
			if (text[i] == '#')
				while (i + 1 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '9') ++i;
		}
		return out;
	}

	std::vector<CompareEvent> ReadCompareEvents(const JsonValue& events) {
		const JsonValue& columns = events["columns"];
		auto column = [&](const char* name) {
			for (size_t k = 0; k < columns.Count(); ++k)
				if (columns.At(k).Str() == name) return k;
			return (size_t)0;
		};
		const size_t eid = column("eid"), parent = column("parent"), kind = column("kind"), hidden = column("hidden"), name = column("name");
		std::vector<CompareEvent> out;
		const JsonValue& rows = events["events"];
		for (size_t i = 0; i < rows.Count(); ++i) {
			const JsonValue& row = rows.At(i);
			CompareEvent e;
			e.eid = (u32)row.At(eid).Uint();
			e.parent = (u32)row.At(parent).Uint();
			e.kind = row.At(kind).Str();
			e.hidden = row.At(hidden).Uint() != 0 || row.At(hidden).Bool();
			e.name = row.At(name).Str();
			out.push_back(e);
		}
		return out;
	}

	std::vector<AlignedEvent> AlignEvents(const std::vector<CompareEvent>& a, const std::vector<CompareEvent>& b, bool sameApi) {
		return Aligner(a, b, sameApi).Run();
	}

	std::vector<StateDifference> DiffState(const JsonValue& a, const JsonValue& b, u32& compared) {
		std::vector<StateDifference> out;
		compared = 2;
		for (const char* field : {"command", "pipeline"}) {
			const std::string va = a[field].Str(), vb = b[field].Str();
			if (WithoutObjectNumbers(va) != WithoutObjectNumbers(vb)) out.push_back({field[0] == 'c' ? "Command" : "Pipeline", va, vb});
		}
		DiffLists(a["groups"], b["groups"], true, "", out, compared);
		return out;
	}

	ImageDifference DiffImages(const float* a, const float* b, u32 width, u32 height, double threshold, std::vector<float>& diff) {
		ImageDifference d;
		diff.assign((size_t)width * height * 4, 0.0f);
		u32 x0 = width, y0 = height, x1 = 0, y1 = 0;
		double sum = 0;
		for (u32 y = 0; y < height; ++y)
			for (u32 x = 0; x < width; ++x) {
				const size_t at = ((size_t)y * width + x) * 4;
				float largest = 0;
				bool odd = false;
				for (u32 c = 0; c < 4; ++c) {
					const float va = a[at + c], vb = b[at + c];
					float delta = 0;
					if (std::isfinite(va) && std::isfinite(vb)) delta = std::fabs(va - vb);
					else if (std::isfinite(va) || std::isfinite(vb) || std::isnan(va) != std::isnan(vb) || (!std::isnan(va) && va != vb)) {
						odd = true;
						delta = NAN;
					}
					if (c < 3) diff[at + c] = delta;
					if (std::isfinite(delta)) largest = std::max(largest, delta);
				}
				diff[at + 3] = 1;
				sum += largest;
				d.largest = std::max(d.largest, (double)largest);
				if (odd) ++d.nan;
				if (!odd && largest <= threshold) continue;
				++d.changed;
				x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x + 1), y1 = std::max(y1, y + 1);
			}
		d.mean = width && height ? sum / ((double)width * height) : 0;
		if (d.changed) {
			d.box[0] = x0, d.box[1] = y0, d.box[2] = x1, d.box[3] = y1;
		}
		return d;
	}
}
