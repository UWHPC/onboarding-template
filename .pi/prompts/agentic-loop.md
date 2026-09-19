---
description: Run the nested coder/design-critic/performance-architect review loop on the UWHPC onboarding submission
argument-hint: "[optional extra focus, e.g. 'optimize OpenMP scaling']"
---

You are the **Orchestrator** (this main session). Run the agentic loop defined in
[`AGENT_ORCHESTRATION.md`](./AGENT_ORCHESTRATION.md) to implement and harden
`src/submission.hpp` for the UWHPC onboarding task.

Task focus: ${ARGUMENTS:-implement the full submission end-to-end per REQUIREMENTS.md and DESIGN.md}

Do this:

1. Read `AGENT_ORCHESTRATION.md`, then `AGENT_LOG.md`. If `AGENT_LOG.md` has an
   unfinished run, resume from its **Current loop state** instead of restarting.
2. Ensure the loop state block and an `INIT` entry exist in `AGENT_LOG.md`.
3. Spawn the `coder` agent (name: `coder`) with the task above.
4. On each coder handoff, spawn/message `design-critic` (name: `design-critic`) to
   review. If it returns `VERDICT: CHANGES_REQUESTED`, message `coder` with the
   findings. Repeat until `VERDICT: PASS`.
5. Then spawn/message `performance-architect` (name: `performance-architect`) to
   review, test, and benchmark. If it returns `CHANGES_REQUESTED`, message `coder`
   with the findings, then return to step 4.
6. Exit the loop only when `performance-architect` returns `VERDICT: PASS`. Append a
   `DONE` entry to `AGENT_LOG.md` and summarize the final state, score, and open NITs.

Commit at the loop boundaries per `AGENT_ORCHESTRATION.md` §5.1: a baseline commit
(`B`) after the coder's first implementation, an inner-loop convergence commit (`CI`)
every time `design-critic` returns `PASS` (before dispatching `performance-architect`),
and a final outer-loop convergence commit (`CO`) when `performance-architect` returns
`PASS`. Never commit code that fails `ctest`, and keep tooling commits separate from
submission commits.

Honour the circuit breakers and escalation rules in `AGENT_ORCHESTRATION.md`. Keep the
same subagent names (`coder`, `design-critic`, `performance-architect`) for the whole
run so their sessions persist. Do not poll for results — the harness delivers them.
