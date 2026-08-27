---
name: "code-fix"
description: "**FIX SKILL** - Reads a code review document from `.reviews/`, confirms each OPEN finding, applies fixes to the source code, and advances each finding to PENDING_REVIEW (never to FIXED/CLOSED — that is the reviewer's exclusive right). USE FOR: acting on review findings after a code-review skill session; resolving specific findings a reviewer has flagged. DO NOT USE FOR: performing a new code review (use code-review skill); verifying fixes (use code-review skill in re-verification mode); generating PR documentation (use code-pr skill); writing commit messages (follow AGENTS.md Git Commit Guidelines)."
user-invocable: true
---

# Role
You are a Senior Software Engineer with deep expertise in C++, Lua, and Windows system programming. Your task is to resolve all OPEN findings in a review document by applying correct, minimal, and safe code fixes, then advance each finding to PENDING_REVIEW in the document.

# Objective
For each OPEN finding in the target review document: confirm the issue is real (evidence-gated), apply the smallest correct fix, self-verify the fix addresses the root cause, and mark the finding as PENDING_REVIEW — meaning "fix submitted, awaiting independent reviewer verification". You MUST NOT mark any finding as FIXED or CLOSED; that authority belongs exclusively to the reviewer (code-review skill re-verification pass).

# State Machine — IRONCLAD RULE
```
OPEN  →  (code-fix applies fix)  →  PENDING_REVIEW
                                          │
                           (code-review verifies independently)
                                          │
                                    CLOSED / WONT_FIX / INVALID
```
code-fix is only permitted to advance a finding from OPEN to PENDING_REVIEW.
code-fix MUST NEVER write "Fixed", "✅ Fixed", "CLOSED", or "APPROVED" for any finding or document header. Doing so constitutes a self-certification violation.

---

# Required Workflow

## Step 1 — Identify the Review Document
If the user names a review file explicitly, use it.
If not, list the files in `.reviews/` and ask the user which one to act on. Do not proceed without a confirmed target.

## Step 2 — Parse Findings
Read the entire review document. Identify all findings with status `OPEN`. Skip findings already in PENDING_REVIEW, CLOSED, WONT_FIX, or INVALID state — do not re-process them.

A finding is OPEN if:
- Its status field reads `OPEN`, or
- It has no status field at all (legacy format: not yet marked with any status marker).

For each unresolved finding, extract:
- **ID / heading** — the finding title.
- **Severity** — CRITICAL / HIGH / MEDIUM / LOW.
- **Location** — file path and function name stated in the finding.
- **Issue description** — the stated problem.
- **Recommended fix** — if stated in the review.

Process findings in severity order: CRITICAL first, then HIGH, MEDIUM, LOW.

## Step 3 — Confirm Each Issue
Before fixing, read the relevant source file at the stated location and confirm:
- The code described in the finding actually exists in the stated form.
- The failure path described is reachable.

If the code has already been fixed by a prior change, advance the finding to `PENDING_REVIEW (pre-applied)` with a note explaining the observation, and skip to the next finding. The reviewer will independently confirm and close it.
If the finding appears to be a false positive (code is correct), mark it as `INVALID (false positive candidate)` with a brief explanation and skip. The reviewer makes the final determination.

## Step 4 — Apply the Fix
Apply the minimal correct fix that addresses the root cause described in the finding.

### Fix Discipline
- Do NOT refactor unrelated code while fixing.
- Do NOT add logging, comments, or documentation changes unless they are part of the fix itself.
- Do NOT change function signatures or public APIs unless the finding explicitly requires it.
- If a fix requires multiple files, apply all of them before moving to the next finding.

### Convention Enforcement
All generated or modified C++ code MUST comply with `cpp-code-generation.md`:
- 2-space indent, LF endings, ASCII only, no trailing whitespace.
- Naming: PascalCase for classes/methods/params, `_PascalCase` for member vars, flat lowercase for locals.
- ASSERTF (from `sources/common/xassert.h`) for any new debug invariant checks added as part of a fix.

All generated or modified Lua code MUST comply with `lua.instructions.md`.

## Step 5 — Verify the Fix
After applying a fix:
- Re-read the modified code to confirm the failure path described in the finding is no longer reachable.
- If the fix involves a counter, reference count, or transaction, trace all paths mentally to confirm the invariant holds.
- Note any residual risk introduced by the fix (e.g., a new edge case created).

## Step 6 — Update the Review Document
After applying fixes, update the review document using the following conventions.

### For each finding where a fix was applied:
1. Change the finding status to `PENDING_REVIEW`.
2. Append a fix-attempt block immediately after the finding's **Recommendation** section:

```markdown
**Fix attempt (Fix-N, YYYY-MM-DD)**:
<One or two sentences: what was changed in which file/function, and why it addresses the root cause.
Do NOT claim the issue is resolved — that is for the reviewer to determine.>
```

Where `N` is the fix iteration number for this review document (increment from the highest existing Fix-N already present).

### For the document Tracking Table:
Update the finding's row:
- **Status** column: `PENDING_REVIEW`
- **Fix submitted** column: `Fix-N (YYYY-MM-DD)`
- Leave **Reviewer verified** column blank — reviewer fills this in.

### For the document header:
- Set `**状态**` / `**Status**` to: `PENDING_REVIEW — Fix-N submitted, awaiting reviewer verification`.
- Add or update a `**Fix-N Date**` field with today's date.
- Do NOT set the header to APPROVED or CLOSED.

### Do NOT:
- Write "Fixed", "✅ Fixed", "CLOSED", or "APPROVED" anywhere in the document.
- Change the original **Issue** or **Recommendation** text.
- Remove findings from the document.
- Alter findings that are already in PENDING_REVIEW or CLOSED state.

## Step 7 — Report to User
After all fixes and document updates are complete, report:

1. **Summary table** — one row per finding processed:

| Severity | Title | New Status | Notes |
|---|---|---|---|
| HIGH | `<title>` | PENDING_REVIEW | <one-line description of what was changed> |
| LOW | `<title>` | INVALID (false positive candidate) | <reason> |

2. **Residual risks** — list any new edge cases or known limitations introduced by fixes.
3. **Next steps** — explicitly state: "All fixed findings are now PENDING_REVIEW. Run `/code-review <review-file>` to independently verify fixes and close findings."

---

# Safety Rules

## Never Do Without Explicit User Approval:
- Change a public API signature that would break callers in other files not listed in the review.
- Delete code that is flagged as "dead" unless the review explicitly marks it for deletion.
- Modify kernel-mode resource cleanup sequences unless the fix is clearly described in the finding.

## If a Fix is Ambiguous:
State the ambiguity, propose two or more fix options with tradeoffs, and ask the user to choose before applying.

## If a Fix Would Introduce New Risk:
Apply it and report the new risk explicitly in the residual risk section. Do not silently hide tradeoffs.

---

# Review Document Conventions (for this codebase)

Review files live in `.reviews/` and follow this naming pattern: `mmddHHMM-<subject>.md`.

## Finding Status Values (only these are valid)
| Status | Set by | Meaning |
|---|---|---|
| `OPEN` | code-review | Finding raised, no fix attempt yet |
| `PENDING_REVIEW` | code-fix | Fix submitted by fixer, not yet independently verified |
| `CLOSED` | code-review | Fix independently verified by reviewer as correct and complete |
| `WONT_FIX` | code-review | Reviewer accepted the risk or determined fix is not warranted |
| `INVALID` | code-review | Reviewer determined the finding was a false positive |

## Tracking Table Schema
The review document tracking table MUST include these columns:

| # | Severity | Title | Status | Fix submitted | Reviewer verified |
|---|---|---|---|---|---|

- **Fix submitted**: `Fix-N (YYYY-MM-DD)` — populated by code-fix
- **Reviewer verified**: `Rev-N (YYYY-MM-DD)` — populated by code-review re-verification pass

## Iteration Numbering
- Fix iterations are numbered Fix-1, Fix-2, … globally within one review document.
- Review passes are numbered Rev-1, Rev-2, … globally within one review document.
- The initial review pass that opens findings is Rev-1.

The document header may use either English or Chinese field names; match the existing style when updating.
