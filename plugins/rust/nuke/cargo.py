"""Rust through cargo (IED docs/26 section 6). Nuke's configurations map onto cargo's profiles - one that does not
optimize is the dev profile, any other `--release`, with debug info when the configuration keeps it
(CARGO_PROFILE_RELEASE_DEBUG for the run, never written into Cargo.toml) - and the artifacts cargo reports (executables,
dynamic and static libraries, their .pdb) are what Nuke copies into build/bin. cargo keeps its own target/ folder,
Cargo.lock and registry cache, so building in IED and outside it share them.

While the workspace develops a package source (`nuke package develop`), the crates the project gets from that
repository's git URL build from its clone: cargo runs with `--config build/develop/<project>/cargo.toml`, whose
[patch."<url>"] points them at the clone's crate folders, and whose resolver.lockfile-path keeps the resolution in a
Cargo.lock beside it (copied from the project's), so an edit in the clone needs no release and nothing of the project
changes (IED docs/31 section 7). `nuke package lock` removes both. For IED's rust-analyzer, editor() writes one
cargo.toml for every Rust project of the workspace in build/develop/, and rust-analyzer.json, the rust-analyzer
profile's options, naming it.

[project.driver]:
package = "name"  a member of a Cargo workspace (default: the crate in the project's folder)
bin = "name"      the program `nuke run` starts (default: the package's name)
"""

import hashlib
import json
import os
import shutil
import tomllib

STAGED_KINDS = {"bin", "cdylib", "dylib", "staticlib"}
NOT_STAGED = (".rlib", ".rmeta", ".d")
DEVELOP_CONFIG = "cargo.toml"
EDITOR_LOCK = "target/nuke-develop/Cargo.lock"   # relative to the Cargo workspace rust-analyzer runs cargo in


def profile_args(configuration):
	return [] if configuration.optimize == 0 else ["--release"]


def profile_env(configuration):
	return {"CARGO_PROFILE_RELEASE_DEBUG": "true"} if configuration.optimize != 0 and configuration.debug_info else None


def selection(ctx):
	args = ["--manifest-path", os.path.join(ctx.root, "Cargo.toml")]
	if ctx.options.get("package"):
		args += ["--package", ctx.options["package"]]
	return args


def artifacts(stdout):
	"""The files of cargo's compiler-artifact messages (one JSON object per line) that a program or another language
	links or runs: executables and dynamic or static libraries, with their debug files; Rust's own libraries are not."""
	found = []
	for line in stdout.splitlines():
		if not line.startswith("{"):
			continue
		message = json.loads(line)
		if message.get("reason") != "compiler-artifact" or not set(message.get("target", {}).get("kind", [])) & STAGED_KINDS:
			continue
		found += [path for path in message.get("filenames", []) if not path.endswith(NOT_STAGED)]
	return found


def develop_args(ctx):
	"""`--config` naming the file that builds the crates the project gets from a develop checkout's repository from its
	clone; [] when it gets none."""
	if not ctx.checkouts:
		return []
	metadata = cargo_metadata(ctx)
	if metadata is None:
		return []                      # the build says what is wrong with the manifest
	lock = os.path.join(metadata["workspace_root"], "Cargo.lock")
	patches = patch_tables(ctx.checkouts, git_crates(metadata, lock))
	if not patches:
		ctx.develop_file(DEVELOP_CONFIG, None)
		return []
	own_lock = os.path.normpath(os.path.join(ctx.develop_dir, "Cargo.lock"))
	copy_newer(lock, own_lock)
	return ["--config", ctx.develop_file(DEVELOP_CONFIG, render_config(patches, own_lock))]


def editor(folder, contexts):
	"""rust-analyzer's view of the develop checkouts (IED docs/31 section 7): cargo.toml with the [patch] tables of every
	Rust project of the workspace, and rust-analyzer.json, the options of the rust plugin's profile - cargo.configPath
	naming cargo.toml (rust-analyzer passes it to every cargo command, metadata included) and, in cargo.extraEnv, a
	lockfile of each Cargo workspace's own in its target/ folder (CARGO_RESOLVER_LOCKFILE_PATH, relative: rust-analyzer
	runs cargo in the Cargo workspace's folder), copied from its Cargo.lock, so the project's lock is never written; with
	the digest of cargo.toml, so a changed one starts rust-analyzer again. Both go when no project gets a crate of a
	clone."""
	patches, roots = {}, []
	for ctx in contexts:
		metadata = cargo_metadata(ctx)
		if metadata is None:
			continue
		roots.append(metadata["workspace_root"])
		for url, crates in patch_tables(ctx.checkouts, git_crates(metadata, os.path.join(metadata["workspace_root"], "Cargo.lock"))).items():
			patches.setdefault(url, {}).update(crates)
	if not patches:
		return {"cargo.toml": None, "rust-analyzer.json": None}
	for root in roots:
		copy_newer(os.path.join(root, "Cargo.lock"), os.path.normpath(os.path.join(root, EDITOR_LOCK)))
	text = render_config(patches, None)
	env = {"CARGO_RESOLVER_LOCKFILE_PATH": EDITOR_LOCK, "NUKE_DEVELOP": hashlib.sha256(text.encode("utf-8")).hexdigest()[:12]}
	options = {"cargo": {"configPath": os.path.normpath(os.path.join(folder, "cargo.toml")), "extraEnv": env}}
	return {"cargo.toml": text, "rust-analyzer.json": json.dumps(options, indent=2) + "\n"}


def cargo_metadata(ctx):
	"""`cargo metadata --no-deps` of the project (its manifests, without resolving); None when cargo fails on them."""
	code, out = ctx.capture([ctx.tool("cargo"), "metadata", "--no-deps", "--format-version", "1", "--manifest-path",
		os.path.join(ctx.root, "Cargo.toml")], check=False)
	return json.loads(out) if code == 0 else None


def git_crates(metadata, lock):
	"""(repository URL, crate) of every crate the project gets from git: what its manifests name (`cargo metadata
	--no-deps`), and what its own Cargo.lock names (the crates those depend on)."""
	found = [(d.get("source") or "", d["name"]) for package in metadata.get("packages", []) for d in package.get("dependencies", [])]
	try:
		with open(lock, "rb") as f:
			found += [(p.get("source") or "", p.get("name", "")) for p in tomllib.load(f).get("package", [])]
	except (OSError, tomllib.TOMLDecodeError):
		pass
	return sorted({(canonical(source[4:]), name) for source, name in found if source.startswith("git+")})


def canonical(url):
	"""A git source's URL as cargo compares them: no reference (?tag=, #commit), trailing slash or .git."""
	url = url.split("?")[0].split("#")[0].rstrip("/")
	url = url[:-4] if url.lower().endswith(".git") else url
	return url.lower() if "github.com" in url.lower() else url


def patch_tables(checkouts, crates):
	"""{URL: {crate: folder}} of the crates whose repository a develop checkout is and that are in its clone."""
	folders, tables = {}, {}
	for url, name in crates:
		checkout = next((c for c in checkouts if c.serves(url)), None)
		if checkout is None:
			continue
		if checkout not in folders:
			folders[checkout] = crate_folders(checkout)
		if name in folders[checkout]:
			tables.setdefault(url, {})[name] = folders[checkout][name]
	return tables


def crate_folders(checkout):
	"""{crate name: folder} of the packages in a develop checkout."""
	found = {}
	for path in checkout.files("Cargo.toml"):
		try:
			with open(path, "rb") as f:
				name = tomllib.load(f).get("package", {}).get("name")
		except (OSError, tomllib.TOMLDecodeError):
			continue
		if isinstance(name, str):
			found.setdefault(name, os.path.normpath(os.path.dirname(path)))
	return found


def copy_newer(lock, copy):
	"""A Cargo.lock that patched builds resolve in, so the project's own stays as it is: the project's, copied again
	whenever it is the newer one."""
	if os.path.isfile(lock) and (not os.path.isfile(copy) or os.path.getmtime(lock) > os.path.getmtime(copy)):
		os.makedirs(os.path.dirname(copy), exist_ok=True)
		shutil.copyfile(lock, copy)


def render_config(patches, lock):
	"""Cargo's [patch] tables, and with `lock` the lockfile cargo resolves them in."""
	lines = ["# Nuke's while a package source is developed: these crates build from the clone in place of their releases",
		"# (IED docs/31 section 7). `nuke package lock` removes this file."]
	for url, crates in sorted(patches.items()):
		lines += ["", f"[patch.{json.dumps(url)}]"] + [f"{name} = {{ path = {json.dumps(folder)} }}" for name, folder in sorted(crates.items())]
	if lock is not None:
		lines += ["", "[resolver]", f"lockfile-path = {json.dumps(lock)}"]
	return "\n".join(lines) + "\n"


def build(ctx):
	argv = [ctx.tool("cargo"), "build"] + develop_args(ctx) + ["--message-format=json-render-diagnostics"] + selection(ctx) + profile_args(ctx.configuration)
	_, stdout = ctx.capture(argv, env=profile_env(ctx.configuration))
	return artifacts(stdout)


def package_name(ctx):
	if ctx.options.get("package"):
		return ctx.options["package"]
	manifest = os.path.join(ctx.root, "Cargo.toml")
	try:
		with open(manifest, "rb") as f:
			return tomllib.load(f).get("package", {}).get("name") or ctx.project.name
	except (OSError, tomllib.TOMLDecodeError):
		return ctx.project.name


def program(ctx):
	if ctx.project.kind == "library":
		return None
	name = ctx.options.get("bin") or package_name(ctx)
	return f"{name}.exe" if ctx.system == "windows" else name


def test(ctx):
	argv = [ctx.tool("cargo"), "test"] + develop_args(ctx) + selection(ctx) + profile_args(ctx.configuration)
	if ctx.test_filter:
		argv.append(ctx.test_filter)
	if ctx.args:
		argv += ["--"] + list(ctx.args)
	return ctx.run(argv, env=profile_env(ctx.configuration), check=False)
