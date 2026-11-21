"""Renders ied.svg at the sizes Windows asks for and packs them into ied.ico (PNG entries); the 256-pixel one is also
ied.png, the Linux window's and applications menu's icon (desktop/linux/DesktopEntry.ch).

	python desktop/resources/make_icon.py        (needs Playwright's Chromium, as tests/e2e does)
"""

import asyncio
import pathlib
import struct

from playwright.async_api import async_playwright

HERE = pathlib.Path(__file__).resolve().parent
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)


async def render(svg):
	images = []
	async with async_playwright() as playwright:
		browser = await playwright.chromium.launch()
		page = await browser.new_page()
		for size in SIZES:
			await page.set_viewport_size({"width": size, "height": size})
			sized = svg.replace("<svg ", f'<svg width="{size}" height="{size}" ', 1)
			await page.set_content(f'<html><body style="margin:0;background:transparent">{sized}</body></html>')
			images.append(await page.screenshot(omit_background=True, clip={"x": 0, "y": 0, "width": size, "height": size}))
		await browser.close()
	return images


def pack(images):
	header = struct.pack("<HHH", 0, 1, len(images))
	entries = b""
	offset = len(header) + 16 * len(images)
	for size, png in zip(SIZES, images):
		edge = 0 if size >= 256 else size
		entries += struct.pack("<BBBBHHII", edge, edge, 0, 0, 1, 32, len(png), offset)
		offset += len(png)
	return header + entries + b"".join(images)


def main():
	svg = (HERE / "ied.svg").read_text(encoding="ascii")
	images = asyncio.run(render(svg))
	(HERE / "ied.ico").write_bytes(pack(images))
	(HERE / "ied.png").write_bytes(images[SIZES.index(256)])
	print("wrote", HERE / "ied.ico", "and", HERE / "ied.png")


if __name__ == "__main__":
	main()
