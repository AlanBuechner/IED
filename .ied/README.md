# .ied/ — IED's own solution hidden folder

This is the committed hidden folder for the IED repo itself — the same pattern every IED solution gets (see `../docs/03-project-and-solution-model.md`). It survives deletion of generated files; per-machine state lives in `.ied/local/` which is gitignored.

```
.ied/
├── skills/            # project skills for working on IED (committed)
│   └── ied-project/SKILL.md
├── solution.json      # (later) solution-tier settings
├── projects/          # (later) project-tier settings
└── local/             # gitignored: workspace layout, build-derived LSP options
```
