# AGENTS.md - Agent Collaboration Guidelines

Welcome to the `dogdouclix` repository. This file defines the guidelines, architecture conventions, and operating principles for AI agents and developers collaborating on this project.

---

## 1. Project Overview

- **Repository**: `dogdouclix`
- **Primary Goal**: Maintain high code quality, clean architecture, and reliable development workflows.

---

## 2. Core Principles for AI Agents

1. **Safety & Stability First**:
   - Avoid destructive operations or accidental file overwrites.
   - Do not remove existing comments, docstrings, or unrelated code unless explicitly instructed.
   - Ensure backwards compatibility where appropriate.

2. **Planning & Transparency**:
   - For complex features or refactors, outline a concise execution plan before modifying files.
   - Keep git diffs minimal, focused, and purposeful.

3. **Context Awareness**:
   - Always verify existing project files and dependencies before introducing new patterns or libraries.
   - Maintain consistency with existing naming conventions, code styles, and architecture.

4. **Verification**:
   - Test and verify changes whenever relevant tools/test suites are available.
   - Ensure scripts and commands are compatible with the current environment (Windows / PowerShell).

---

## 3. Code Quality & Engineering Standards

- **Language Requirement**: All documentation, code, comments, docstrings, and related repository artifacts MUST be written entirely in English.
- **Readability & Modularity**: Write self-documenting code with clear separation of concerns.
- **Error Handling**: Handle edge cases gracefully and avoid swallowing exceptions silently.
- **Environment & Secrets**: Never hardcode credentials, API keys, or secrets; use environment variables (`.env`).
- **File Encoding**: Use standard UTF-8 encoding for all source files.

### Language-Specific Code Generation Instructions

* Before generating or modifying C++ code, agents MUST read and follow `.agents/instructions/cpp-code-generation.md`.
* These files contain detailed, language-specific rules. `AGENTS.md` intentionally keeps only repository-wide constraints and the required routing to those rules.

---


## 4. Project Memory Guide

* Use `.agents/MEMORY.md` for project-specific details, debugging tips, implementation traps, long-term observations, and highly contextual facts that are too specific for `AGENTS.md`.
* Before debugging recurring issues or changing code in an area with known history, check `.agents/MEMORY.md` for relevant notes.
* When a durable project-specific lesson is discovered, update `.agents/MEMORY.md` instead of expanding `AGENTS.md` with detailed implementation trivia.
* Keep memory entries concise, factual, and tied to concrete files, functions, symptoms, or error codes whenever possible.

## Documentation Synchronization

* After each meaningful implementation stage is complete, review and update any related documents in `documents/` to keep them consistent with the code.
* A "stage" is any logical unit of work: completing a new module, changing an API contract, adding/removing a protocol step, or refactoring a significant subsystem.
* Documents to check: architecture overviews, design documents, PR write-ups, and flow diagrams that describe the changed area.
* Do not leave stale documentation that contradicts the implemented behavior.

--

## 5. Git Commit Guidelines

Before committing code, agents MUST run `git diff --check` to ensure that the commit not contains whitespace errors. All whitespace errors MUST be fixed before committing.

All git commit messages MUST be written entirely in English and strictly follow the `Title[ + blank line + details]` format:

```
Title

1. somethings 1.
2. somethings 2.
```

* **Title (Mandatory):**
  * Imperative mood describing the theme (e.g. `Add ...`, `Update ...`, `Remove ...`, `Standardize ...`).
  * First letter capitalized.
  * No conventional prefixes (do NOT use `feat:`, `fix:`, `chore:`, `feat/`, `fix(scope):`, etc.).
* **Details (Optional):**
  * Separated from the title by a blank line.
  * Numbered list starting with `1. `, `2. `, etc.
  * Each line MUST be in all lowercase (except technical proper nouns, contains the first letter of a sentence).
  * Each line MUST end with a period (`.`).
  * Describes the detailed changes and rationale.

---

## 6. DogdouSpec Workflow

This repository uses **DogdouSpec** to manage iterations, specifications, and tasks through authoritative XML documents in `.dogdouspec/`.

1. **Use Repo-Local CLI**:
   - Windows: `.\dogdouspec.cmd <command>`
   - Do not install global tools or configure external MCP servers for DogdouSpec.
2. **Never Directly Edit `.dogdouspec/*.xml`**:
   - Do not use text editors, scripts, or direct file writes on files inside `.dogdouspec/`.
   - All managed mutations must be executed through the public CLI (`task update`, `append`, `transaction apply`, `iteration confirm`).
3. **Discover & Select Actionable Work (Two-Phase Query)**:
   - Discover workspace: `.\dogdouspec.cmd workspace discover --format xml`
   - Validate workspace: `.\dogdouspec.cmd validate --format xml`
   - List active iterations: `.\dogdouspec.cmd iteration list --format xml`
   - Query in-progress task (Phase 1a):
     ```powershell
     .\dogdouspec.cmd query --document "<ITERATION_ID>/tasks.xml" --xpath "ds:filter(/tasks/task[@status='in-progress' or @status='verification'][1], '@id', '@status', '@agent', 'index')" --format xml
     ```
   - Query next pending task (Phase 1b):
     ```powershell
     .\dogdouspec.cmd query --document "<ITERATION_ID>/tasks.xml" --xpath "ds:filter(/tasks/task[@status='pending' and not(dependencies/ref[@relation='depends-on']/@target = /tasks/task[@status!='done' and @status!='transferred' and @status!='superseded' and @status!='cancelled']/@id)][1], '@id', '@status', '@agent', 'index')" --format xml
     ```
   - Load full selected task (Phase 2):
     ```powershell
     .\dogdouspec.cmd query --document "<ITERATION_ID>/tasks.xml" --xpath "/tasks/task[@id='<TASK_ID>']" --format xml
     ```
4. **Follow the Checked-In Skill**:
   - Read [`.agents/skills/dogdouspec/SKILL.md`](.agents/skills/dogdouspec/SKILL.md) and its references for complete workflow rules, XPath projections, mutation semantics, and authority rules.
5. **Task Updates & State Transitions**:
   - Transition task: `pending` -> `start` (`in-progress`) -> `verify` (`verification`) -> `complete` (`done`).
   - Always pass exact expected revisions (`--expected-revision <N>`).
   - Validate workspace after each mutation: `.\dogdouspec.cmd validate --format xml`.
6. **Respect Product Authority Gates**:
   - Technical agents cannot auto-complete requirements, design decisions, or iterations.
   - Run `.\dogdouspec.cmd iteration readiness` to check gating status.
   - Only execute `iteration confirm` when explicitly instructed by the human product owner in the current interaction.
