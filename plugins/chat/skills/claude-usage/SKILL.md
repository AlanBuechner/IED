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

The 5-hour session and the shared week limit every Claude model. The Opus and Sonnet weeks, and Fable's own week ("Fable
week"), are limits of that model's on top of them: Fable is stopped by the shared week like the others, and by its own.
When the owner sets the week's pace to the work week (`ai.claude.paceDays`), a weekly window's pace counts only the work
days: it stands still over a weekend, and `ai_usage` says "through its work days".

A window used up does not keep Claude out of a turn: every turn asks Claude first (the plan may have been upgraded
mid-week, or the reading may be off), Claude's own refusal moves the turn to the next model of its chain, and the next
turn asks Claude again.

`ai_usage` shows every window with its pace and reset, the models of `ai.models`, and for each group chain with a Claude
model which model a turn uses now and why the ones before it are passed over. Read it before handing out work, and again
in a long run.

## Choosing models

- **Under pace (every window at or below 1.0x):** use the strongest model for the hard parts - planning, a tricky bug,
  a design. Room left near a reset is lost at the reset.
- **Ahead of pace:** keep the strongest model for the decisions and give routine work - renames, mechanical edits,
  searching, writing tests that follow a pattern - to a helper chat on a cheaper model:
  `chat_create({"title": "Rename the fields", "text": "...", "model": "claude-sonnet"})` or `claude-haiku` for the simplest.
- **A window used up** (the 5-hour session or the shared week): Claude will likely refuse every model it limits - Fable
  too - and each turn then falls to its chain's local model after Claude says so. Say so to the owner, and give the work a
  local model can do to a chat pinned to it, rather than waiting on refusals; Claude is asked again each turn, so a plan
  upgraded meanwhile is used at once.
- `chat_set_model({"model": "claude-sonnet", "chat": "<id>"})` moves a chat's next turns; `""` gives it back its group's
  model. A chat's running turn keeps its model: switching mid-turn would read the whole conversation into a new cache.
- A group's chain with paces (the `claude` group's: opus, sonnet, haiku, then fable) already steps down as
  the windows run ahead of pace; pinning is for when you know the work better than the pace does.

## Not to do

- Never set up, ask for or use an API key for Claude: IED runs Claude on the subscription only, and refuses a CLI signed
  in any other way.
- Do not spend a strong model's usage on output nobody reads (long restatements, logs pasted back).
