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

---

## 4. Git Commit Guidelines

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
