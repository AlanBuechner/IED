---
name: claude-usage
description: Making the owner's Claude subscription last - reading the plan's 5-hour session and weekly windows and their pace with ai_usage, and choosing which model each chat runs on (chat_set_model, chat_create with a model) so the strongest model is used while there is room and routine work goes to cheaper ones.
triggers:
  - "claude usage"
  - "usage limit"
  - "session limit"
  - "weekly limit"
  - "rate limit"
  - "which model"
  - "save usage"
  - "stretch the usage"
---

# Claude's usage

Claude runs on the owner's Claude subscription through their own Claude Code CLI. The plan has windows: a 5-hour session
and a week, and some models have a weekly window of their own. Each has a utilization (percent used), a reset, and a
**pace**: its use against the even line that runs out exactly at the reset. 1.00x lasts to the reset; 1.50x runs out with
a third of the window left; 0.50x leaves half of it unused.

`ai_usage` shows every window with its pace and reset, the models of `ai.models`, and what a paced (`model: "auto"`) profile
picks now. Read it before handing out work, and again in a long run.

## Choosing models

- **Under pace (every window at or below 1.0x):** use the strongest model for the hard parts - planning, a tricky bug,
  a design. Room left near a reset is lost at the reset.
- **Ahead of pace:** keep the strongest model for the decisions and give routine work - renames, mechanical edits,
  searching, writing tests that follow a pattern - to a helper chat on a cheaper model:
  `chat_create({"title": "Rename the fields", "text": "...", "model": "claude-sonnet"})` or `claude-haiku` for the simplest.
- **A window used up:** Claude turns go to the group's fallback (a local model) until it frees; say so to the owner and
  give the work a local model can do to a chat pinned to it.
- `chat_set_model({"model": "claude-sonnet", "chat": "<id>"})` moves a chat's next turns; `""` gives it back its group's
  model. A chat's running turn keeps its model: switching mid-turn would read the whole conversation into a new cache.
- A profile with `model: "auto"` already steps down the ladder (opus, sonnet, haiku) as the windows run ahead of pace;
  pinning is for when you know the work better than the pace does.

## Not to do

- Never set up, ask for or use an API key for Claude: IED runs Claude on the subscription only, and refuses a CLI signed
  in any other way.
- Do not spend a strong model's usage on output nobody reads (long restatements, logs pasted back).
