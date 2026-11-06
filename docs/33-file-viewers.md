# 33 - File viewers

## Purpose

Any file opens in something sensible (US-EDT-11, F-EDT-95, F-EDT-96, F-EDT-98): pictures and game textures, sound,
video, PDFs and fonts in viewers of their own, every other binary as hex, and the text editor never loads a binary as
text. The owner (2026-11-06): "the text editor should be able to open images, videos, audio, and any other common file
types."

Status: **built 2026-11-06.**

## Where it lives

The viewers are the `ied.filetypes` plugin's (`plugins/filetypes`, declarative with a UI module): it already gave
these files their icons, and now contributes their `fileViewers` (`docs/02`). The host's part is generic and named by no
viewer: the `fileViewers` point (`docs/02`), the workspace's files over HTTP with byte ranges (`docs/10` "Transport"),
and `doc.open` refusing a binary file (`docs/10` "Document editing protocol").

| Viewer | Tab | Opens by default | Also in Open With... |
|---|---|---|---|
| Image Viewer | `image:<path>` | PNG, APNG, JPEG, GIF, WebP, BMP, ICO, AVIF, TGA, DDS, Radiance HDR | SVG |
| Media Player | `media:<path>` | MP3, WAV, OGG, Opus, FLAC, M4A, AAC, WebA; MP4, M4V, WebM, OGV, MOV, MKV, AVI, WMV | |
| PDF Viewer | `pdf:<path>` | PDF | |
| Font Viewer | `font:<path>` | TTF, TTC, OTF, WOFF, WOFF2 | |
| Hex Viewer | `hex:<path>` | executables, libraries, objects, PDBs, `.bin`, `.dat`, `.wasm`, `.class`, `.pyc`, archives, PSD, EXR | every file |

## Opening a file

A file opens in its default viewer wherever a file opens - the Solution Explorer, Go To All, `ied open` (`docs/02`
"fileViewers"). **Open With...** lists the Text Editor and each viewer that takes the file, once each, the default marked.

**A binary file in the text editor.** `doc.open` refuses a file holding a NUL among its first 8000 bytes (as search and
git tell binaries) with `bad_request`, data `{reason: "binary", size}`, unless the request says `binary: true`: then the
document opens read-only (`readOnly`, `readOnlyReason` "a binary file: its text is only shown"), since text never
round-trips a binary's bytes. The editor puts a notice over the tab instead of the text: a button for each viewer a
plugin has for the file (the default one first and primary; the hex view takes any file) - choosing one replaces the
tab - and **Show Its Text, Read-only**. A binary only reaches the text editor through Open With or a file no viewer
claims (`save.xyz`).

**SVG** is text first, as in Visual Studio. Its tab strip's **Open Preview to the Side** (and the "+" menu) opens its
picture beside it, tethered (`docs/15` "Tethered tabs"); the picture follows the document as it is typed - unsaved
edits too - re-drawn 150 ms after the last change, keeping the last good picture while the text does not parse.

Every viewer reloads its file when it changes on disk (the workspace's `tree.changed`), keeping its place: the image's
zoom, a sound's or video's time, the hex view's selection; a deleted file says so. Each has **Download** (a copy to the
device: a phone's downloads, the desktop's Downloads folder), and so has the Solution Explorer's menu on a file.

## The image viewer

**Formats.** The browser decodes PNG, APNG, JPEG, GIF, WebP, AVIF, BMP and ICO, read exactly as stored
(`createImageBitmap` without premultiplying or color conversion, so the readout shows the file's bytes). The viewer
decodes the game formats itself (`ui/image/`):

- **TGA**: color-mapped, true-color and grayscale; raw or run-length; 8, 15, 16, 24 and 32 bits; either corner; a
  32-bit image that claims no alpha and stores zero everywhere is opaque.
- **DDS**: legacy and DX10 headers; BC1 to BC7 (BC6H unsigned and signed as floats; BC4/BC5 signed shown as (v + 1) / 2;
  one channel as gray, two in red and green); R8G8B8A8, B8G8R8A8/X8, B5G6R5, B5G5R5A1, B4G4R4A4, R10G10B10A2, R8,
  R8G8, A8, R16, R16G16, R16G16B16A16 (unorm and float), R32, R32G32, R32G32B32(A32) float, R11G11B10 float,
  R9G9B9E5; legacy masks (RGB, luminance, alpha) and Direct3D 9's float FourCCs; typeless and sRGB variants as their
  layouts. Array elements, cube faces (`+X` ... `-Z`), mips and a volume's slices are chosen in the toolbar; a smaller
  mip keeps the size the picture had on screen. sRGB values show as stored.
- **Radiance HDR** (`.hdr`, RGBE): flat and run-length scanlines, either vertical order.

Checked against Pillow 12.3 on the DirectX samples' textures (BC1, BC3, BC7 cube, BC6H cubes - every BC6H mode occurs in
them - TGA) and on 512 random blocks of every BC6H and BC7 mode: exact for BC1-BC5, BC7 and TGA, within 1 of 255 for
BC6H (rounding). Signed BC6H follows DirectXTex (its sign extension after the delta transform); Pillow differs there.

**Drawing.** WebGL2 (`glview.js`): one triangle over the view, its fragment shader finding its texel - nearest texels
magnified, trilinear mipmaps minified (floats as RGBA16F, linear) - so channels, the checkerboard, the grid and exposure
change at once on any device. The page draws (`domview.js`) what WebGL should not: an SVG (sharp at every zoom), an
animated GIF, PNG or WebP (it keeps moving), a picture past the GPU's largest texture, a browser without WebGL2.

**Tools.** The picture fits the view (never enlarged) until zoomed: the wheel or a pinch about the pointer, `+` / `-`
and the buttons by steps (1/64 to 256x), `0` fits, `1` is actual size, a double click switches the two; a drag or the
arrows pan, the picture kept in sight. **R G B A**: one color channel shows as gray, alpha alone as gray, alpha over a
checkerboard while A is on. The texel grid shows from 8x. A float picture (HDR, a float DDS, BC6H) has **EV**, its
exposure in stops (double-click resets), then sRGB encoding. The status line reads the texel under the pointer, or a tap
on a phone: bytes with their hex (`#FF8000FF`), floats with four decimals.

Not yet: OpenEXR and PSD (they open as hex), ASTC and ETC, a texture's histogram and range, editing.

## Sound and video

The browser's own player, fed by the server's byte ranges, so a long video seeks at once; speed and loop; length,
resolution, channels, sample rate and size in the toolbar. A sound up to 64 MiB has its waveform (Web Audio's decode,
every channel's peaks), lighter where it has played, a click or tap seeking. A video pauses when its tab is hidden; a
sound plays on. A file whose format or codec the browser does not play (AVI, WMV and most MKV, on most browsers) says so,
with the hex view and Download.

## PDF

The browser's own viewer in the tab (Edge, so WebView2 and IED's desktop app, and Chrome: pages, zoom, search, print);
the file is served without the sandboxing policy, which Chromium's viewer refuses. A browser that shows no PDF in a page
(`navigator.pdfViewerEnabled === false`: Chrome on Android) offers the browser's own viewer and Download. The Claude
desktop app's browser pane has no PDF viewer (it downloads PDFs) and still claims one, so it shows an empty tab.

## Fonts

The font loaded into the page (`FontFace`) and shown: its family and style, kind, glyph count and size; its full name,
version and copyright (OpenType's `name` and `maxp` tables, read from TTF, OTF, a collection's first font, and WOFF's
inflated tables; WOFF2's are Brotli-coded, so only its kind); sample text you type over at eight sizes; printable ASCII
and Latin-1.

## Hex

Read-only, as Visual Studio's binary editor shows: offsets as wide as the file needs, 16 bytes a row (8 or 4 on a narrow
view), their characters (`.` for the rest, dimmed, as are zeros). Only the rows in sight are fetched, in 64 KiB ranges,
the 96 most recent kept; past 8 million pixels of rows the scroll position is scaled to the rows, so a file of gigabytes
opens and scrolls at once. A click or the arrows, Page Up/Down, Home/End (Ctrl for the file's ends) select a byte, shown
in both columns and read by the data inspector - binary, signed and unsigned 8 to 64 bits, Float32, Float64, the UTF-8
character starting there - little- or big-endian (`LE` / `BE`); the inspector is the status line on a narrow view. **Go
to offset** (Ctrl+G) takes `0x1F`, `1Fh`, `$1F` or `31`.

Not yet: editing bytes, find.

## The server's part

`GET /w/<wsId>/files/<path>[?download=1]` (`src/web/WorkspaceFiles.cf`, `docs/10` "Transport"):

- Any file of the workspace (never outside it), as its type for the viewers - pictures, SVG, sound, video, PDF, fonts,
  plain text - and as `application/octet-stream` with `Content-Disposition: attachment` for the rest; `?download=1`
  makes any file an attachment (RFC 8187's name, so any name). Nothing is sniffed, and every type but PDF goes with
  `Content-Security-Policy: default-src 'none'; ... sandbox`: an SVG or HTML file opened by itself runs no script on
  IED's origin.
- One byte range (`Range: bytes=a-b`, `a-`, `-n`): `206` with `Content-Range`, at most 4 MiB a range (an open range is
  cut there; the client asks for the rest); `416` past the end; several ranges or another unit answer the whole file.
  Ranges are never gzipped. Without a range a file of at most 256 MiB is sent whole (`413` past that).
- `ETag` (size and modification time) with `If-None-Match` (`304`) and `If-Range`; `Cache-Control: no-cache`.

The master passes the request to the workspace's server as any `/w/<wsId>/` request, headers and status included.

## Checks

Frontend unit `filetypes.test.js` (13: the decoders against synthetic files and Pillow's vectors, the view's zoom math,
animation detection, readouts, hex rows and the inspector, font names); server check `test_file_viewers.py` (types and
policies, ranges, ETags, binary `doc.open`); e2e `files_open_in_their_viewers` (real server, desktop and phone: a TGA's
toolbar and tapped texel, a binary in the text editor taken to the hex view); screenshots of every viewer at desktop and
phone width on the private build, and the PDF tab in Edge.
