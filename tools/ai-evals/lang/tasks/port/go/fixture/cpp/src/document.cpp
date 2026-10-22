#include "document.h"

#include "inline.h"
#include "lines.h"
#include "lists.h"
#include "text.h"

namespace mdhtml {
namespace {

class BlockParser {
public:
    BlockParser(const std::vector<std::string_view>& lines, std::string& out) : lines_(lines), out_(out) {}

    void Run() {
        while (pos_ < lines_.size()) {
            switch (Classify(lines_[pos_])) {
                case LineKind::Blank: ++pos_; break;
                case LineKind::Fence: WriteCodeBlock(); break;
                case LineKind::Heading: WriteHeading(); break;
                case LineKind::Rule: WriteRule(); break;
                case LineKind::Quote: WriteQuote(); break;
                case LineKind::ListItem: WriteList(); break;
                case LineKind::Text: WriteParagraph(); break;
            }
        }
    }

private:
    // The lines up to a closing fence, or to the end of the lines when none comes, kept as they are.
    void WriteCodeBlock() {
        Fence fence = *MatchFence(lines_[pos_++]);
        out_ += "<pre><code";
        if (!fence.language.empty()) {
            out_ += " class=\"language-";
            AppendEscaped(out_, fence.language);
            out_ += "\"";
        }
        out_ += ">";
        while (pos_ < lines_.size() && !ClosesFence(lines_[pos_], fence.ticks)) {
            AppendEscaped(out_, lines_[pos_++]);
            out_ += "\n";
        }
        if (pos_ < lines_.size()) ++pos_;
        out_ += "</code></pre>\n";
    }

    void WriteHeading() {
        Heading heading = *MatchHeading(lines_[pos_++]);
        std::string tag = "h" + std::to_string(heading.level);
        out_ += "<" + tag + ">" + RenderInline(heading.text) + "</" + tag + ">\n";
    }

    void WriteRule() {
        out_ += "<hr />\n";
        ++pos_;
    }

    // Consecutive quote lines; their content is parsed as blocks of its own.
    void WriteQuote() {
        std::vector<std::string_view> content;
        while (pos_ < lines_.size()) {
            std::optional<std::string_view> inner = MatchQuote(lines_[pos_]);
            if (!inner) break;
            content.push_back(*inner);
            ++pos_;
        }
        out_ += "<blockquote>\n";
        RenderBlocks(content, out_);
        out_ += "</blockquote>\n";
    }

    // List lines and the text lines that continue their items; blank lines only between two list lines.
    void WriteList() {
        std::vector<ListItem> items;
        while (pos_ < lines_.size()) {
            LineKind kind = Classify(lines_[pos_]);
            if (kind == LineKind::ListItem) {
                items.push_back(MakeListItem(*MatchListItem(lines_[pos_])));
            } else if (kind == LineKind::Text) {
                items.back().text += '\n';
                items.back().text += Trim(lines_[pos_]);
            } else if (kind != LineKind::Blank || !ListResumesAfter(pos_)) {
                break;
            }
            ++pos_;
        }
        RenderList(items, out_);
    }

    bool ListResumesAfter(size_t blank) const {
        size_t next = blank;
        while (next < lines_.size() && IsBlank(lines_[next])) ++next;
        return next < lines_.size() && Classify(lines_[next]) == LineKind::ListItem;
    }

    // Consecutive text lines, trimmed and joined with '\n'; any other kind of line ends the paragraph.
    void WriteParagraph() {
        std::string text(Trim(lines_[pos_++]));
        while (pos_ < lines_.size() && Classify(lines_[pos_]) == LineKind::Text) {
            text += '\n';
            text += Trim(lines_[pos_++]);
        }
        out_ += "<p>" + RenderInline(text) + "</p>\n";
    }

    const std::vector<std::string_view>& lines_;
    std::string& out_;
    size_t pos_ = 0;
};

}  // namespace

std::string RenderDocument(std::string_view text) {
    std::string out;
    RenderBlocks(SplitLines(text), out);
    return out;
}

void RenderBlocks(const std::vector<std::string_view>& lines, std::string& out) { BlockParser(lines, out).Run(); }

}  // namespace mdhtml
