import os
import subprocess
import time

import Utils.Utils as Utils
from Utils.Windows import *
import Config


# The command that runs `args` with everything after the program in a response file at `path`: a module's objects
# in a deep folder (a git worktree) outgrow the 32767 characters a Windows command line may have.
def ResponseFileCommand(args, path):
	quoted = ['"' + arg.replace("\\", "\\\\").replace('"', '\\"') + '"' for arg in args[1:]]
	with open(path, "w") as f:
		f.write(" ".join(quoted))
	return [args[0], "--rsp-quoting=posix", f"@{path}"]


def LinkObjects(intDir, links, projDir, outputFile, linkerArgs, buildType, needsBuild):

	# fix lib paths
	for i, link in enumerate(links):
		lib = Utils.FindLib(link, projDir)
		if(lib == ""):
			print(f"Failed to find link: {link}")
		links[i] = lib

	# get args
	args = []
	args.append(Config.compiler)
	args.extend(["-target", Utils.GetTarget()])

	if(buildType == Config.BuildType.EXECUTABLE):
		args.extend(["-fuse-ld=lld"])
		if(Config.configuration != "Dist" or Config.useDistPDBs == True):
			args.extend(["-g"])
	elif(buildType == Config.BuildType.STATIC_LIBRARY):
		args.extend(["-fuse-ld=llvm-lib"])
	elif(buildType == Config.BuildType.DYNAMIC_LIBRARY):
		args.extend(["-shared", "-fPIC"])
		# A DLL gets its .pdb as an executable does, so a debugger or profiler sees its functions (C4Script.dll).
		if(Config.configuration != "Dist" or Config.useDistPDBs == True):
			args.extend(["-g"])
	# An archive only collects objects; executables and DLLs link the sanitizer runtime (the clang driver adds the
	# runtime and its thunk library itself).
	if(Config.useAsan == True and buildType != Config.BuildType.STATIC_LIBRARY):
		args.append("-fsanitize=address")
		args.append("-fno-sanitize-address-use-odr-indicator")

	args.extend(["-o", outputFile])
	files = Utils.ResolveFiles([f"{intDir}/**.obj"], intDir)
	args.extend(files)
	args.extend(links)
	args.extend(linkerArgs)

	# determines if build is needed
	lastBuildTime = 0.0
	# check if file does not exist
	if(not os.path.isfile(outputFile)):
		needsBuild = True
	else:
		# check if any links are newer than this build
		lastBuildTime = os.path.getmtime(outputFile)

	# a recompiled object must always force a relink; without this, a change to a single
	# source (e.g. one test file in an executable) recompiles its .obj but the exe is never
	# relinked because the libs and args are unchanged, so the build silently ships stale code.
	for obj in files:
		if os.path.getmtime(obj) > lastBuildTime:
			needsBuild = True

	# check last build time of links
	for link in links:
		if(link == ""):
			continue
		buildTime = os.path.getmtime(link)
		if(buildTime > lastBuildTime):
			needsBuild = True

	# check if args have changed
	argsFile = os.path.join(intDir, "linker.args")
	formattedArgs = " ".join(args)
	if(os.path.exists(argsFile)):
		with open(argsFile) as f:
			lastArgs = f.read()
			if(lastArgs != formattedArgs):
				needsBuild = True
	else:
		needsBuild = True

	linkTime = 0.0
	linkStartTime = 0.0
	linkEndTime = 0.0
	if(needsBuild):
		# write args file
		with open(argsFile, "w") as f:
			f.write(formattedArgs)

		projName = os.path.basename(projDir)
		startTime = time.perf_counter()
		linkStartTime = time.time()
		result = subprocess.run(ResponseFileCommand(args, os.path.join(intDir, "linker.rsp")), cwd=projDir, capture_output=True, text=True)
		endTime = time.perf_counter()
		linkEndTime = time.time()
		linkTime = endTime - startTime
		log = f"|------------- linking project : {projName} -------------|\n"
		log += str(result.stderr)
		log += str(result.stdout)
		log += f"linking {projName} completed in {linkTime:.4f}s with error code {result.returncode}\n"
		print(log)
		return (result.returncode, linkTime, linkStartTime, linkEndTime, result.stderr)
	return (-1, 0.0, 0.0, 0.0, "")