# Context compaction

The texts IED's compaction uses (docs/04 "Context management", plugins/chat/code/context/Compaction.ch). When a local
model's context passes its limit, the model is asked to summarize everything before a cut (`system` and `request`,
once per chunk of the transcript, each chunk folded into the summary so far); requests then send `block` right after
the system message, in place of what was cut, followed by the newest messages as they were. Placeholders are in braces.

## system
You write the working memory of a coding agent whose conversation has outgrown its context window. Another instance of the same agent will continue the work with only your summary, the system message and the newest messages in front of it - it cannot see the conversation you are reading. Everything it needs to carry on without asking again or redoing work must be in your summary. Write only the summary: no tool calls, no preamble, no closing remarks.

## request
{previous}

Below is {part} of the conversation to fold into the summary. Write the complete summary again - everything above that still matters, with this part added - under exactly these headings:

### Goal and the owner's requests
Every request and instruction the owner gave, in order, with the newest last; quote their exact words where wording matters (requirements, names, preferences, corrections, things they said not to do). Say which are done and which are still open.

### Key context
Facts the work depends on: the project's conventions and constraints, decisions made and why, approaches tried and rejected and why, how things were verified.

### Files and code
Each file read, created or changed that still matters: its path, what it holds or what was changed and why, and any exact code, signatures, identifiers, commands or error text that will be needed again - quoted exactly, not paraphrased.

### Errors and fixes
Errors met (build, test, tool, runtime) and how each was resolved, or that it is still unresolved.

### Progress
What has been done, in order, with results: what passed, what failed, what is verified and what is not.

### Current state
Exactly what was being worked on at the end of this part: the task, the file and place in it, the last action taken and its result.

### Next steps
The immediate next action, and the rest of the plan that remains, consistent with the owner's newest request. Do not add steps the owner did not ask for.

Keep it dense and specific: paths, names, numbers and quotes over prose. Leave out pleasantries, abandoned dead ends that teach nothing, and file contents that can simply be read again (name the file instead). Aim for at most {words} words.

<conversation>
{transcript}
</conversation>

## previous_none
There is no summary yet: this part is the start of the conversation.

## previous_some
The summary so far, of the conversation before this part:

<summary>
{summary}
</summary>

## block
[Context summary {count}] This chat's earlier context was compacted to fit the model's context window: the summary below stands for everything before it, and the messages after it follow as they were sent. Nothing is lost - history_search and history_read give back any earlier turn whole, and files can be read again; reread a file before editing it if its exact current content matters.

{summary}

## block_state
What IED kept for certain when it compacted:
{state}
