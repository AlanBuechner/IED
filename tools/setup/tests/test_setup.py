"""setup.bat's MSVC kit (tools/setup/msvc_kit.py): a VSIX's Contents/ folder unpacked with its percent-encoded names and
nothing outside it, an SDK installer fetched with the CAB files it names (and an error for one the manifest lacks),
packages found by id and language, the SDK's libraries for other processors removed; and setup's record of what it
installed, which for the kit covers the packages it takes from the manifest."""

import io
import os
import shutil
import sys
import tarfile
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import msvc_kit  # noqa: E402
import setup  # noqa: E402


class KitTest(unittest.TestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-setup-test-")

	def tearDown(self):
		shutil.rmtree(self.root, ignore_errors=True)

	def path(self, *parts):
		return os.path.join(self.root, *parts)


class UnpackTests(KitTest):
	def test_only_the_contents_folder_with_decoded_names(self):
		vsix = self.path("package.vsix")
		with zipfile.ZipFile(vsix, "w") as archive:
			archive.writestr("Contents/VC/Tools/MSVC/14.51.36231/include/my%20header.h", "a")
			archive.writestr("Contents/VC/Tools/MSVC/14.51.36231/lib/x64/libcmt.lib", "b")
			archive.writestr("[Content_Types].xml", "c")
			archive.writestr("manifest.json", "d")
			archive.writestr("Contents/../outside.txt", "e")
		staging = self.path("staging")
		msvc_kit.unpack_vsix(vsix, staging)
		found = sorted(os.path.relpath(os.path.join(base, name), staging).replace("\\", "/") for base, _, names in os.walk(staging) for name in names)
		self.assertEqual(["VC/Tools/MSVC/14.51.36231/include/my header.h", "VC/Tools/MSVC/14.51.36231/lib/x64/libcmt.lib"], found)
		self.assertFalse(os.path.exists(self.path("outside.txt")), "a name leaving the folder is skipped")


class InstallerTests(KitTest):
	def payload(self, name):
		return {"fileName": f"Installers\\{name}", "url": f"https://example.invalid/{name}", "sha256": name[:8]}

	def fetcher(self, fetched):
		def fetch(url, sha256, path, label):
			fetched.append(os.path.basename(path))
			os.makedirs(os.path.dirname(path), exist_ok=True)
			with open(path, "wb") as f:
				f.write(b"MSI ... 0123456789abcdef0123456789abcdef.cab ... FEDCBA9876543210FEDCBA9876543210.cab ..." if path.endswith(".msi") else b"cab")
			return path
		return fetch

	def by_name(self, *names):
		return {payload["fileName"].replace("\\", "/"): payload for payload in map(self.payload, names)}

	def test_the_installer_and_the_cabs_it_names_side_by_side(self):
		fetched = []
		cabs = ("0123456789abcdef0123456789abcdef.cab", "FEDCBA9876543210FEDCBA9876543210.cab")
		msi = msvc_kit.fetch_installer(self.by_name("Libs-x86_en-us.msi", *cabs, "unrelated.cab"), "Libs-x86_en-us.msi", self.path("sdk"), self.fetcher(fetched))
		self.assertEqual(self.path("sdk", "Libs-x86_en-us.msi"), msi)
		self.assertEqual(["Libs-x86_en-us.msi", cabs[0], cabs[1]], fetched, "only the CAB files it names")

	def test_a_cab_the_package_lacks_is_an_error(self):
		with self.assertRaises(msvc_kit.KitError) as missing:
			msvc_kit.fetch_installer(self.by_name("Libs-x86_en-us.msi", "0123456789abcdef0123456789abcdef.cab"), "Libs-x86_en-us.msi", self.path("sdk"), self.fetcher([]))
		self.assertIn("FEDCBA9876543210FEDCBA9876543210.cab", str(missing.exception))

	def test_an_installer_the_package_lacks_is_an_error(self):
		with self.assertRaises(msvc_kit.KitError) as missing:
			msvc_kit.fetch_installer({}, "Libs-x86_en-us.msi", self.path("sdk"), self.fetcher([]))
		self.assertIn("Libs-x86_en-us.msi", str(missing.exception))


class ManifestTests(unittest.TestCase):
	MANIFEST = {"packages": [
		{"id": "Win11SDK_10.0.26100", "language": "de-DE", "payloads": [{"fileName": "de"}]},
		{"id": "Win11SDK_10.0.26100", "payloads": [{"fileName": "neutral"}]},
		{"id": "Microsoft.VC.14.51.CRT.Headers.base", "payloads": []},
	]}

	def test_a_package_by_id_in_english_or_neutral(self):
		self.assertEqual("neutral", msvc_kit.package_named(self.MANIFEST, "win11sdk_10.0.26100")["payloads"][0]["fileName"])

	def test_a_missing_or_empty_package_is_an_error(self):
		with self.assertRaises(msvc_kit.KitError):
			msvc_kit.package_named(self.MANIFEST, "Win11SDK_10.0.99999")
		with self.assertRaises(msvc_kit.KitError):
			msvc_kit.payloads_of(self.MANIFEST, "Microsoft.VC.14.51.CRT.Headers.base")


class PruneTests(KitTest):
	def test_only_the_kept_processors_libraries_stay(self):
		lib = self.path("Windows Kits", "10", "Lib", "10.0.26100.0")
		for part in ("um/x64", "um/x86", "um/arm64", "ucrt/x64", "ucrt/arm64"):
			os.makedirs(os.path.join(lib, *part.split("/")))
		msvc_kit.prune_architectures(self.root, ["x64"])
		self.assertEqual(["x64"], os.listdir(os.path.join(lib, "um")))
		self.assertEqual(["x64"], os.listdir(os.path.join(lib, "ucrt")))


class RecordTests(unittest.TestCase):
	def test_the_kits_record_covers_what_it_takes_from_the_manifest(self):
		packages = setup.load_packages()
		kit, llvm = packages["msvc"], packages["llvm"]
		self.assertEqual(llvm["sha256"], llvm["fingerprint"], "an archive is recorded by its own hash")
		self.assertEqual(setup.kit_fingerprint(kit), kit["fingerprint"])
		self.assertNotEqual(kit["sha256"], kit["fingerprint"])
		for field in ("msvc-packages", "sdk-installers", "architectures"):
			changed = dict(kit, **{field: kit[field][:-1]})
			self.assertNotEqual(kit["fingerprint"], setup.kit_fingerprint(changed), f"another {field} is another install")

	def test_what_a_linux_package_leaves_out_or_installs_for_is_part_of_its_record(self):
		packages = setup.load_packages()
		python, tor, wheels, windows = packages["python-linux"], packages["tor-linux"], packages["searxng-packages-linux"], packages["searxng-packages"]
		self.assertEqual(setup.choice_fingerprint([python["sha256"], python["skip"]]), python["fingerprint"])
		self.assertNotEqual(python["fingerprint"], setup.choice_fingerprint([python["sha256"], python["skip"][1:]]))
		self.assertNotEqual(tor["sha256"], tor["fingerprint"], "a skip list is part of it")
		self.assertNotEqual(wheels["sha256"], wheels["fingerprint"], "so is the platform")
		self.assertEqual(windows["sha256"], windows["fingerprint"], "Windows' own wheels are recorded by the requirements alone")
		self.assertEqual([], setup.foreign_platform(windows))
		options = setup.foreign_platform(wheels)
		self.assertEqual(["--python-version", "3.12", "--implementation", "cp", "--abi", "cp312"], options[:6])
		self.assertEqual(["manylinux_2_28_x86_64", "manylinux_2_17_x86_64", "manylinux2014_x86_64"], options[7::2])
		self.assertEqual({"linux"}, {python["group"], tor["group"], wheels["group"]}, "never installed by default")


class SkipTests(KitTest):
	def test_skipped_paths_are_left_out_of_an_archive(self):
		archive = self.path("python.tar.gz")
		with tarfile.open(archive, "w:gz") as tar:
			for name in ("python/bin/python3.12", "python/lib/libpython3.12.so.1.0", "python/lib/python3.12/os.py", "python/include/Python.h", "python/includes.txt"):
				data = name.encode()
				info = tarfile.TarInfo(name)
				info.size = len(data)
				tar.addfile(info, io.BytesIO(data))
			link = tarfile.TarInfo("python/bin/python3")
			link.type, link.linkname = tarfile.SYMTYPE, "python3.12"
			tar.addfile(link)
		staging = self.path("staging")
		setup._extract_tar(archive, staging, 1, "r:gz", [["include"], ["lib", "libpython3.12.so.1.0"]])
		found = sorted(os.path.relpath(os.path.join(base, name), staging).replace("\\", "/") for base, _, names in os.walk(staging) for name in names)
		self.assertEqual(["bin/python3.12", "includes.txt", "lib/python3.12/os.py"], found, "a folder with all it holds, a file, never a name that only begins alike; links are left out")


if __name__ == "__main__":
	unittest.main()
