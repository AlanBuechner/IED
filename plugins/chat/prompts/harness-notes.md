# Harness note templates (sent as user-role notes tagged origin=harness; see docs/12)

These are the exact texts the agent runtime sends when it recovers a stopped run. Keep them short, specific and free of blame - small local models follow short notes best. `{...}` are filled by the runtime.

## continue_after_max_tokens
Your reply was cut off by the output limit. Continue exactly where you stopped - do not repeat what you already wrote.

## continue_after_disconnect
The connection dropped while you were replying. Continue exactly where you stopped - do not repeat what you already wrote.

## retry_tool_call_after_cut
Your last tool call was cut off before its arguments were complete. Send the {tool} call again, complete.

## repeated_output
Your last reply was stopped and discarded because it kept repeating the same text. Do not go over it again: decide the next concrete step and take it with a tool call, keeping your reasoning short.

## empty_reply
Your previous reply contained no answer and no tool call. Respond now: call a tool, or give your answer.

## reasoning_only
You finished thinking but did not reply. Act on your reasoning now: call a tool, or give your answer.

## announced_no_action
You said you would {action} but did not call a tool. Make the tool call now, or explain why you are not doing it.

## agent_no_finish
You stopped without calling finish. If the work is complete and verified, call finish(summary, verification). Otherwise continue with the next step.

## endless_no_finish
This chat runs until the owner stops it: there is no finish. Record what the last step gave if you have not, then go on with the next step toward the goal - the next thing to try, measure, refine or fix - with a tool call.

## endless_finish_refused
This chat runs until the owner stops it: there is no finish. Record what was done (a commit, a note), report it in a few words, and go on with the next step toward the goal.

## open_tasks_on_finish
Tasks {tasks} are still open (by id and title). Complete them, or mark them skipped with a reason - task_update({"id": ..., "status": "done"}) or "skipped" with a note - before finishing.

## owes_reply_on_finish
Not finished: this chat owes a reply to the chats below - each required one (require_reply) and may be suspended until it comes. Send each its reply first with {call}: what you did, what you found and what is left. Then finish.
{chats}

## owes_reply_on_suspend
Not suspended: this chat owes a reply to the chats below, and nothing you would wait on was asked for after it, so both chats could end up waiting on each other. Send the reply first with {call}, then suspend if you still need to wait - or wait here with chat_wait or background_wait.
{chats}

## endless_suspend_refused
Not suspended: this chat runs until the owner stops it, and nothing would wake it - no chat owes it a reply and none of its background tasks runs. Suspend only to wait for those; go on with the next step.

## invalid_tool_args
The arguments for {tool} were invalid: {error}. Expected schema: {schema_excerpt}. Call it again with corrected arguments.

## unknown_tool
There is no tool named {name}. Did you mean {suggestions}? tool_search finds the deferred tools (marked [deferred]) by what they do.

## unknown_clip
{tool} did not run: its arguments paste {{clip:{name}}}, and there is no clip {name} ({clips}). Copy it with clipboard_copy first. To write {{clip:{name}}} as text, write {{!clip:{name}}}.

## interrupted_call
The call {tool} ({call_id}) was interrupted by a restart; its effect is unknown. Check the current state before retrying it.

## steering
[Message from the user, sent while you were working]: {text}

## budget_warning
You have used {percent}% of this run's {budget} budget. Wrap up: finish the current task, verify, and report.

## context_compressed
Earlier parts of this conversation were left out of your requests to save space - not summarized and not lost: they are still in this chat, whole, and a restart does not empty it. `history_search` finds where anything was said and `history_read` gives a turn back; read the files or symbols again if you need their exact contents.

## tasks_after_compaction
The conversation was compacted to fit the context, and its summary may leave out your task list. The chat's tasks, as they stand:
{tasks}
Keep them current with task_update - finish refuses while any is pending or in_progress.

## unparsed_tool_call
Your reply wrote a tool call as text that could not be read. Call the tool again in the tool-calling format, with complete JSON arguments.

## refused_call
The permission rules refused {tool}: {why}. Do not retry it; continue another way, or ask the user with ask_user.

## denied_by_user
The user denied {tool}.{message} Do not retry it unchanged; continue another way, or ask the user what they want.

## user_answer
The user answered: {answer}

## name_chat
This chat has no name yet: its tab shows the first words of this message. With your first reply, call chat_title with 3 to 6 words on what the work is, so the user can find it among their tabs. Do not mention this in your answer.
