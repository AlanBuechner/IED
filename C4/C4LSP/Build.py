# C4LSP build script - the language server executable: a stdio loop over C4LSPLib
import os
import inspect
projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")

import ProjectManager
import Config

def GetProject():
    p = ProjectManager.Project()
    p.group = "C4"
    p.includes = [f"{projDir}/src"]
    p.moduleLinks = ["Core", "C4Lib", "ObjectDescription", "C4Protocol", "C4LSPLib"]
    p.buildType = Config.BuildType.EXECUTABLE
    p.links = ["ntdll.lib"]  # LLVMSupport (RtlGetLastNtStatus), reached through the target's host detection
    p.genReflection = False
    # EARLY so a normal root build (-b) produces the server binary alongside C4.exe. It only
    # depends on EARLY modules (Core, C4Lib, ObjectDescription, C4LSPLib), so it can be built in that pass.
    p.buildStage = Config.BuildStage.EARLY
    return p
