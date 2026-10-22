# mdhtml

`mdhtml <file.md>` converts a Markdown file - the subset below, with its own exact rules - to HTML on standard
output.

- No argument, or more than one: `usage: mdhtml <file.md>` on standard error, exit code 2.
- A file that cannot be read: `error: cannot read <file.md>` on standard error, exit code 1.
- Otherwise the HTML on standard output (lines end with `\n` on every system), exit code 0. An empty document
  writes nothing.

## Building

C++20, no dependencies. With clang (or any C++20 compiler):

```
clang++ -std=c++20 -O2 src/*.cpp -o mdhtml.exe
clang++ -std=c++20 -O2 -Isrc src/text.cpp src/lines.cpp src/inline.cpp src/lists.cpp src/document.cpp tests/*.cpp -o mdhtml-tests.exe
```

`mdhtml-tests.exe` runs every test and exits with 0 when all pass.

| File | What it holds |
|---|---|
| `src/text.*` | whitespace, splitting lines, HTML escaping |
| `src/lines.*` | what kind of block a single line starts (heading, fence, rule, quote, list item) |
| `src/lists.*` | list items, nested by indentation, as HTML |
| `src/document.*` | the block parser: walks the lines and writes each block |
| `src/inline.*` | the inline parser: emphasis, code spans, links, escapes |
| `src/main.cpp` | the command line |
| `tests/` | a tiny test runner (`check.*`) and the tests of each source file |

## Lines

The file is read as bytes. It is split into lines at `\n`; a `\r` at the end of a line is dropped, and a `\n` at the
end of the file starts no further line. Whitespace is a space or a tab. A line is **blank** when it holds nothing
but whitespace. A line's **indentation** is the number of whitespace characters it starts with (a tab counts as one).

## Blocks

Every line is classified by what follows its leading whitespace, tested in this order:

1. **blank**
2. **fence**: three or more backticks; the rest of the line, trimmed, is the info string, and its first word (up to
   whitespace) is the language. An info string that contains a backtick makes the line no fence.
3. **heading**: one to six `#`, then whitespace or the end of the line. Seven `#`, or `#word`, is not a heading.
4. **rule**: with all whitespace removed, three or more `-`, or three or more `*` (not mixed).
5. **quote**: `>`.
6. **list item**: `-` or `*`, or one to nine digits and `.`; then whitespace or the end of the line. `1)` is not a
   marker, nor `-item`, nor `1.5`.
7. **text**: anything else.

A rule wins over a list item: `- - -` and `* * *` are rules. The blocks, each written followed by `\n`:

- **Paragraph**: consecutive text lines, each trimmed of whitespace at both ends, joined with `\n`:
  `<p>...</p>`. Any line that is not text (a blank line or the start of any other block) ends it; there is no lazy
  continuation of other blocks and no setext heading (`---` under a paragraph is a rule).
- **Heading**: `<hN>...</hN>`. The text is trimmed; then a closing run of `#` is removed when it is all the text or
  follows whitespace (`# Title ##` is `Title`, `# C#` stays `C#`, `# ###` is empty), and the rest trimmed again.
- **Rule**: `<hr />`.
- **Code block**: from a fence to the next line whose content (after leading whitespace) is a run of at least as
  many backticks followed only by whitespace, or to the end of the lines when no such line comes (the end of the
  document, or of the quote it is in). The lines between are kept exactly as they are (leading whitespace too),
  HTML-escaped, each followed by `\n`: `<pre><code class="language-go">...</code></pre>`, without the `class`
  attribute when there is no language (escaped like text). No other line ends a code block.
- **Blockquote**: consecutive quote lines. Each loses its leading whitespace, the `>`, and one whitespace character
  after it if there is one; those contents are parsed as blocks of their own (so `>>` nests, and a quote can hold
  headings, code, lists): `<blockquote>\n...</blockquote>`. A line without `>` - blank or not - ends the quote.
- **List**: see below.

## Lists

A list starts at a list item line and takes:

- list item lines: each starts an item. Its **level** is its indentation divided by two, rounded down (two spaces
  per level). Its text is what follows the marker, trimmed.
- text lines: each is added to the last item's text, trimmed, after a `\n` (as a paragraph joins lines).
- blank lines, only when the next line that is not blank is a list item line.

Anything else ends the list: a heading, fence, rule or quote line, or a blank line not followed by more items. Items
hold text only (inline Markdown, no `<p>`), and lists inside them.

The items become nested lists, walked in order with the lists still open:

- An item deeper than the open list opens a new list inside the open item. Skipped levels nest only once.
- An item shallower than the open list closes the lists deeper than itself first. When that closes every open list
  (an item shallower than the first one), it starts a new list at the top.
- An item on the open list's level continues it when it is of the same kind (ordered or not; `-` and `*` are the
  same kind). Of the other kind, it closes that list and opens a new one in the same place.

An unordered list is `<ul>`. An ordered list is `<ol>`, or `<ol start="N">` when the number of its first item is not
1 (`007.` is 7; later numbers do not matter). An item is `<li>` + its text, then `\n` and its nested lists if it has
any, then `</li>`:

```
<ul>
<li>a
<ul>
<li>b</li>
</ul>
</li>
<li>c</li>
</ul>
```

## Inline text

The text of a paragraph, heading or list item is parsed left to right:

- **Backslash escape**: `\` followed by ASCII punctuation (``!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~``) is that character
  as plain text. A backslash before anything else, or at the end, is itself.
- **Code span**: a run of N backticks opens a code span that ends at the next run of exactly N backticks; inside it
  nothing is parsed (backslashes too are literal). Line breaks in it become spaces; then, if it begins and ends with a
  space and is not all spaces, one space is removed from each end: `<code>...</code>`. A run with no closing run is
  plain text.
- **Link**: `[text](url)`. The text runs to the `]` that balances the `[` (brackets nest; escaped characters and code
  spans inside are skipped); the `(` must follow the `]` at once; the url runs to the first `)` that is not escaped
  (`\` + punctuation in the url is that character) and must hold no whitespace (no titles). The url may be empty:
  `<a href="url">text</a>`. The text is parsed as inline text on its own, with its own emphasis, where `[` is plain
  text: links do not nest. Anything that is not a whole link leaves the `[` as plain text, and parsing goes on after it.
  There are no images (`![a](b)` is `!` and a link), autolinks or reference links.
- **Emphasis** comes from runs of `*` (`_` is plain text). A run can open emphasis when the character after it is not
  whitespace (a space, a tab or a line break), and close it when the character before it is not. After the whole
  text is read, each run that can close, from left to right, pairs with the nearest run before it that can open and
  has `*` left, again and again while both have some: two `*` from each make `<strong>` when both have at least two,
  else one from each makes `<em>`. The tags an opener gets go right after its remaining `*`, the ones a closer gets
  right before them, and runs inside a pair are out of reach of anything outside it. Unused `*` stay as text.
  `***a***` is `<em><strong>a</strong></em>`, `**a*` is `*<em>a</em>`, `*a**b*` is `<em>a</em><em>b</em>`.
- Everything else is plain text.

**HTML escaping:** in plain text, code spans, code blocks, urls and languages, `&` `<` `>` `"` are written as
`&amp;` `&lt;` `&gt;` `&quot;`; nothing else is changed (`'` stays). Raw HTML and entities are text: `<b>` is
`&lt;b&gt;`, `&amp;` is `&amp;amp;`. There are no hard line breaks.
