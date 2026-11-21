# TODOs

This file reveals the current jobs we are working on.
If the TODO has been completed, please move the record to `todo-archived.md`.
Do not remove titles even when the list has been cleared, just move them to `todo-archived.md`.

## Pending

- (Bug) Dropdown list background is transparent on Windows.
- (Refinement) Queued steer messages might be multi-line, as the bubble should show a "..." behind, avoid making it multi-lined or extend the height, also, a "edit" button should be on the left of the close button, clicking on it should put this message back to the input box (append a new row if the input box is not empty)


## Active

- (Bug) A failed turn is now visible where the Doctor can find it: `bell::client::prompt`
  logs the failure through `felis::log` itself (once, at the tail, with the reason)
  and reports it to the GUI as its return value — `prompt_result { code, error }` —
  instead of the abolished `on_done` callback. `error_code` gained a fine-grained
  taxonomy (`ERR_CANCELLED`, `ERR_REQUEST_TRANSPORT`, `ERR_REQUEST_HTTP`,
  `ERR_REQUEST_STREAM`, `ERR_COMPACTION_FAILED`, `ERR_BLANK_REPLY`,
  `ERR_NOTHING_TO_UPDATE`) mapped straight from the transport's `FailureKind`, so
  the planned automatic re-attempt can decide what is retryable without parsing
  prose. `update_memory` returns the same shape, and the transcript notice now
  carries a copy button.

- (Feature) A turn that dies for a transport or stream reason is now re-attempted
  automatically. `client::prompt` wraps the old body (`prompt_once`) in a retry
  loop: an escaping exception becomes `ERR_INTERNAL` instead of unwinding past the
  caller, `ERR_REQUEST_TRANSPORT`, `ERR_REQUEST_STREAM` and `ERR_INTERNAL` are
  retried, and an unknown exit is retried only when the attempt stored no new row
  (so a partial turn is never duplicated). `begin_turn` is never re-run. Attempts
  and the interval come from `retry_policy` (set in the Configuration page, saved
  in `settings.json`, read live by `ConversationController`); a cancel wakes the
  sleep immediately. The bulky rationales inside `prompt_once` were lifted into a
  doc comment above it, matching `prompt_events`.
  - (Delayed) `ERR_REQUEST_HTTP` is deliberately not retried yet: `felis::net::llm::Failure`
    carries no numeric status, so 429 cannot be told from 400. Opting 429/5xx in
    needs a small felis change (`Failure` gaining `status`).
