# Nuke/ — build system kit

Ported from `C:\GameDev\C4\Nuke` once the C4 compiler can build this repo. IED's projects are built with this kit, not with any IED-internal build logic (see `../docs/06-build-and-nuke-integration.md`).

Expected contents after port (mirrors the C4 kit):

```
Nuke/
├── nuke.py            # main entry: action parsing (build|run|test|clean|list), flags, config aliases
├── Config.py          # configurations (Debug/Development/Dist), arch (x64), system (windows),
│                      #   output layout build/{bin,int}/{config}-{system}-{arc}/{proj}, compiler path
├── ProjectManager.py  # per-project Project.py loading, module graph, cycle detection
├── ObjectBuilder.py   # incremental compile decisions (args cache + dependency tree)
├── ProjectBuilder.py  # per-project build environment, post-build copies
├── TestRunner.py      # test project discovery (src/tests/), execution, exit-code aggregation
├── BuildInfo.py       # build-info.json + tracing output
├── ProjectInit.py     # `nuke --init` scaffolding (project files + .ied/ scaffold)
├── IEDConfig.py       # NEW: the editor bridge (replaces EchoConfig.py for IED workspaces):
│                      #   writes .ied project/solution files on init, regenerates
│                      #   .ied/local/lsp-options/<proj>.json after every build
└── Utils/             # shared helpers
```

Repo root gets a `nuke.bat` launcher (like the C4 repo) delegating here.
