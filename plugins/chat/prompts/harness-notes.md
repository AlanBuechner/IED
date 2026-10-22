# Harness note templates (sent as user-role notes tagged origin=harness; see docs/12)

These are the exact texts the agent runtime sends when it recovers a stopped run. Keep them short, specific and free of blame - small local models follow short notes best. `{...}` are filled by the runtime.

## continue_after_max_tokens
Your reply was cut off by the output limit. Continue exactly where you stopped - do not repeat what you already wrote.

## continue_after_disconnect
The connection dropped while you were replying. Continue exactly where you stopped - do not repeat what you already wrote.

## retry_tool_call_after_cut
Your last tool call was cut off before its arguments were complete. Send the {tool} call again, complete.

## empty_reply
Your previous reply contained no answer and no tool call. Respond now: call a tool, or give your answer.

## reasoning_only
You finished thinking but did not reply. Act on your reasoning now: call a tool, or give your answer.

## announced_no_action
You said you would {action} but did not call a tool. Make the tool call now, or explain why you are not doing it.

## agent_no_finish
You stopped without calling finish. If the work is complete and verified, call finish(summary, verification). Otherwise continue with the next step.

## open_tasks_on_finish
Tasks {tasks} are still open. Complete them, or mark them skipped with a reason, before finishing.

## invalid_tool_args
The arguments for {tool} were invalid: {error}. Expected schema: {schema_excerpt}. Call it again with corrected arguments.

## unknown_tool
There is no tool named {name}. Did you mean {suggestions}? Tool groups can be enabled with tools_enable.

## unknown_clip
{tool} did not run: its arguments paste {{clip:{name}}}, and there is no clip {name} ({clips}). Copy it with clipboard_copy first. To write {{clip:{name}}} as text, write {{!clip:{name}}}.

## interrupted_call
The call {tool} ({call_id}) was interrupted by a restart; its effect is unknown. Check the current state before retrying it.

## steering
[Message from the user, sent while you were working]: {text}

## budget_warning
You have used {percent}% of this run's {budget} budget. Wrap up: finish the current task, verify, and report.

## context_compressed
Earlier parts of this conversation were summarized to save space. Use history_search or history_read if you need exact details from them.

## unparsed_tool_call
Your reply wrote a tool call as text that could not be read. Call the tool again in the tool-calling format, with complete JSON arguments.

## refused_call
The permission rules refused {tool}: {why}. Do not retry it; continue another way, or ask the user with ask_user.

## denied_by_user
The user denied {tool}.{message} Do not retry it unchanged; continue another way, or ask the user what they want.

## user_answer
The user answered: {answer}
