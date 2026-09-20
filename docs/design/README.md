# Design documents

The specs and plans this board was built from, kept because they record *why*
the code is shaped the way it is — which is the part that source comments and
commit messages tend to lose.

- `specs/` — what each feature had to do, and the constraints it had to hold.
  The design spec is the one the firmware comments cite by section number
  ("spec 3a", "spec 8").
- `plans/` — the step-by-step implementation plans each feature was actually
  built from, including the risks called out beforehand and, at the end of
  each, how they really landed.

These are historical records, not living documentation. Where one of them
disagrees with the code, the code is right; where one disagrees with
[`../at-api-notes.md`](../at-api-notes.md) about AT's API, the notes are right,
because they were verified against live endpoints afterwards.

They were written to be executed task-by-task by an agentic coding tool, so the
plans carry some scaffolding aimed at that — checkboxes, sub-skill preambles.
Ignore it; the design reasoning underneath is the point.
