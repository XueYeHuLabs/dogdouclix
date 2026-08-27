# DogdouSpec Issues & Evaluation Log

This document records issues, architectural discrepancies, friction points, and operational observations encountered while experimenting with **DogdouSpec** in the `dogdouclix` repository.

> [!NOTE]
> DogdouSpec is currently in experimental evaluation mode for workflow orchestration. All DogdouSpec operational artifacts remain local and uncommitted until evaluation is concluded.

---

## Issue Ledger

### ISSUE-001: Workspace Skill Discovery Path Discrepancy

- **Date Logged**: 2026-08-25
- **Severity**: Medium (Workflow & Agent Discovery)
- **Component**: Installation Guide / Agent Customization Standard
- **Source Reference**: `L:\dogdou\dogdouspec\docs\INSTALL_IN_OTHER_REPOSITORY.md` (Section 5.2)

#### Description
The DogdouSpec deployment guide (`INSTALL_IN_OTHER_REPOSITORY.md`) specifies installing the agent skill into `<TARGET_REPO>/skills/dogdouspec/` and referencing it in `AGENTS.md` as `skills/dogdouspec/SKILL.md`.

However, the Google Antigravity Agent Customization Framework discovers project-specific workspace skills by scanning `<TARGET_REPO>/.agents/skills/<skill-name>/SKILL.md` (or `.agent/skills/`, `_agents/skills/`). Placing the skill at the project root (`skills/dogdouspec/`) prevents the Antigravity runtime from automatically indexing and registering `dogdouspec` in the primary agent's active skills catalogue.

#### Impact
- The agent cannot automatically detect the `dogdouspec` skill through progressive disclosure.
- The agent must rely solely on manual links or explicit file reads rather than native skill registration.

#### Local Workaround Applied
- Migrated skill folder from `skills/dogdouspec/` to `.agents/skills/dogdouspec/`.
- Updated `.agents/skills/dogdouspec/SKILL.md` reference in `AGENTS.md`.

#### Upstream Recommendation
- Update `INSTALL_IN_OTHER_REPOSITORY.md` and repository templates in `L:\dogdou\dogdouspec` to place skills under `.agents/skills/dogdouspec/` (or provide configurable skill targets).

---

### ISSUE-002: Upstream Source Test Timestamp Drift on Non-Backdating Validations

- **Date Logged**: 2026-08-25
- **Severity**: Low (Upstream Testing / Verification)
- **Component**: `DogdouSpec.Core.Tests` and `DogdouSpec.Cli.Tests`

#### Description
When building and running the upstream test suite (`dotnet test DogdouSpec.slnx`), several integration tests (e.g. `RequirementPropose_ProposedStatus_Succeeds`, `ChangePropose_HappyPath_AttachesFindingFreezesTaskAndProposesRequirement`, `TaskQuick_Cli_PendingDryRunAndAtomicStart_ArePubliclyObservable`) failed with:
`decided_at timestamp '2026-08-24T...' is earlier than iteration updated_at timestamp '2026-08-25T...'. Backdating is not permitted.`

The test suite hardcoded `2026-08-24` dates in test fixtures, while `iteration create` uses the current real-time UTC timestamp (`2026-08-25`). Because DogdouSpec strictly enforces non-backdating rules, running tests on subsequent days triggers validation failures.

#### Impact
- CLI build (`DogdouSpec.Cli`) compiles and publishes without issue in Release mode.
- Automated tests in upstream repository fail when executed on dates after the hardcoded fixture dates.

#### Upstream Recommendation
- Introduce a mockable or relative `TimeProvider` / `Clock` in test harnesses so fixture timestamps are always dynamic relative to the test iteration creation timestamp.

---

### ISSUE-003: Task Completion Dual-Schema Coupling Requirement (<acceptance> and <covers>)

- **Date Logged**: 2026-08-25
- **Severity**: Medium (Developer / Agent Ergonomics)
- **Component**: Task State Machine & Schema Validation (`TaskUpdateType`)

#### Description
When executing `task update --transition complete` to mark a task as `done`, DogdouSpec's business rules enforce a strict dual coupling:
1. The request must contain an `<acceptance>` block reporting every criterion: `<criterion target="..." result="passed" />`.
2. The request must ALSO contain a `<records>` element with at least one record of `kind="completion"`, which in turn MUST have a `<covers>` block containing matching `<ref scope="document" target="..." relation="covers" />` elements.

If only `<acceptance>` is provided, or if `<covers>` is omitted inside the completion record, validation rejects the mutation with schema/semantic errors.

#### Impact
- Agents or developers constructing XML mutations by hand or script may easily omit one of the two redundant references.
- Causes mutation rollbacks and requires multiple roundtrips to inspect XSD schemas.

#### Upstream Recommendation
- Consider auto-generating the completion record's `<covers>` references when all task criteria in `<acceptance>` are marked `passed`, or document this strict dual requirement explicitly in the main CLI reference and `SKILL.md`.

---

### ISSUE-004: Decision Vocabulary Asymmetry in iteration confirm (approved vs accepted)

- **Date Logged**: 2026-08-25
- **Severity**: Low (Grammar Consistency)
- **Component**: Confirmation Subsystem (`iteration confirm`)

#### Description
During `iteration confirm`:
- For `action="activate"`, requirements listed under `<requirements>` must have attribute `decision="approved"`. Specifying `decision="accepted"` triggers an immediate error: `INVALID_ARGUMENT: Invalid requirement decision 'accepted' for target '...'`.
- For `action="complete"`, acceptance criteria listed under `<acceptance>` must have attribute `decision="accepted"`.
- The top-level confirmation attribute itself uses `decision="accepted"`.

#### Impact
- Asymmetric vocabulary creates confusion for automated agents and human operators when drafting confirmation requests across activation and completion phases.

#### Upstream Recommendation
- Accept both `approved` and `accepted` interchangeably as valid decision tokens for requirements, or clearly map allowed values in CLI diagnostic output.

---

### ISSUE-005: Atomic Revision Invalidation on Auto-Receipt Records

- **Date Logged**: 2026-08-25
- **Severity**: Low (Concurrency & State Tracking)
- **Component**: CLI Mutation Engine (`task add`, `task update`)

#### Description
Each successful mutation (such as `task add`) automatically appends an informational receipt record (e.g. `<summary>Task addition receipt.</summary>`) to the target XML document. This atomically increments the document's revision number (e.g. from revision 3 to 4).

If an agent attempts multiple batched mutations without re-querying or tracking the exact revision increment (+1 per mutation), subsequent commands fail with `REVISION_MISMATCH`.

#### Upstream Recommendation
- Keep this strict OCC (Optimistic Concurrency Control) design as it guarantees data integrity, but ensure `SKILL.md` explicitly reminds agents to increment their tracked revision counter after each successful CLI execution without requiring extra `validate` calls.

---

---

### ISSUE-006: Task Index Term `status` Never Updated After State Transitions

- **Date Logged**: 2026-08-25
- **Severity**: High (Data Correctness / Index Integrity)
- **Component**: Task Mutation Engine (`task update`)
- **Affected Iterations**: All four — `20260825-cli-forwarding`, `20260825-transparent-proxy`, `20260825-cred-manager-profiles`, `20260825-config-scaffolding`

#### Description

Every task carries an `<index>` block whose `<term key="status">` is intended to be a fast-queryable field summarizing the task's current state. However, CLI mutations that perform state transitions (`start`, `verify`, `complete`) do **not** update this index term. As a result, every completed task in the workspace shows `value="pending"` in its index even though the task's `@status` attribute correctly reflects `done`.

#### Evidence

All 17 tasks across the four iterations share this identical pattern. Representative excerpt from
`.dogdouspec/20260825-cli-forwarding/tasks.xml` (task revision 23, confirmed via `validate` passing):

```xml
<task id="20260825-task-repo-bootstrap" status="done"
      created_at="2026-08-25T01:18:00Z"
      started_at="2026-08-25T01:23:30Z"
      completed_at="2026-08-25T01:25:00Z">
  <index>
    <summary>Bootstrap DogdouClix solution, projects, and build infrastructure.</summary>
    <term key="task"   value="20260825-task-repo-bootstrap"/>
    <term key="status" value="pending"/>   <!-- BUG: should be "done" -->
  </index>
  ...
</task>
```

The same pattern is visible in every task of `20260825-transparent-proxy/tasks.xml`,
`20260825-cred-manager-profiles/tasks.xml`, and `20260825-config-scaffolding/tasks.xml`.

#### Impact

- Any tool or dashboard that queries `index/term[@key='status']` for aggregated task reporting will return `pending` for all tasks, including completed ones.
- The two-phase XPath query pattern in `SKILL.md` explicitly uses `@status` on the `<task>` element (not index terms) for filtering — which means the bug is currently invisible during normal agent workflows. But if index terms were ever used as a secondary search/filter dimension (which their schema design implies), results would be incorrect.

#### Upstream Recommendation

Option A: When CLI executes a `task update` with any `transition`, atomically apply a `set-attribute`
on `task/index/term[@key='status']/@value` to mirror the new `@status` value.\
Option B: Remove `<term key="status">` from the task index entirely and document that task status
is authoritative only via `@status` — preventing the divergence by design.

---

### ISSUE-007: Backlog and Knowledge Documents Remain Empty After Multiple Completed Iterations

- **Date Logged**: 2026-08-25
- **Severity**: High (Process Completeness / Knowledge Continuity)
- **Component**: Project-Level Documents (`backlog.xml`, `knowledge.xml`)
- **Affected Scope**: Workspace-level, cross-iteration

#### Description

After completing two full feature iterations and beginning a third, both project-level documents are
empty. There is no process step or CLI enforcement that prompts population of either document at
iteration boundaries.

#### Evidence

`backlog.xml` (revision 1, never updated since creation):

```xml
<backlog id="20260825-backlog" schema_version="1.0" revision="1">
  <index>
    <summary>Project obligations not owned by an active Iteration.</summary>
    <term key="scope" value="project"/>
  </index>
  <items/>   <!-- Empty — no items ever added -->
</backlog>
```

`knowledge.xml` (revision 1, never updated):

```xml
<knowledge id="20260825-knowledge" schema_version="1.0" revision="1">
  <index>
    <summary>Verified reusable project knowledge.</summary>
  </index>
  <!-- No entries whatsoever -->
</knowledge>
```

Meanwhile, each completed `spec.xml` contains `<excluded>` items describing explicitly deferred scope:

- `20260825-cli-forwarding/spec.xml`: "GUI process virtualization or window wrapping", "Non-Windows platform forwarding"
- `20260825-transparent-proxy/spec.xml`: "Dynamic DLL injection or API hooking", "Non-Windows platform shims"
- `20260825-cred-manager-profiles/spec.xml`: "Third-party cloud secrets managers"

None of these deferred items were promoted to backlog entries.

#### Impact

- Deferred scope from completed iterations is invisible at the project level; the only way to find it is to re-read individual `spec.xml` files.
- Verified technical knowledge (e.g. `CommandLineToArgvW` roundtrip behavior, PATH penetration anti-recursion strategy confirmed by test suite) exists only in task completion records inside per-iteration XML files — not in a dedicated, queryable knowledge base.
- The `knowledge.xml` and `backlog.xml` documents provide zero value in their current state and mislead reviewers into thinking nothing was deferred.

#### Upstream Recommendation

- Add a `iteration complete` pre-flight check that warns if excluded scope items exist in `spec.xml` without corresponding `backlog.xml` entries.
- Add `task complete` or `iteration complete` hooks (or post-completion CLI prompts) suggesting relevant knowledge entries from task `<records>` of kind `completion`.
- Document the intended authoring workflow for `knowledge.xml` and `backlog.xml` in `SKILL.md` (currently absent).

---

### ISSUE-008: No Formal Cross-Iteration Dependency References

- **Date Logged**: 2026-08-25
- **Severity**: Medium (Traceability / Impact Analysis)
- **Component**: Task Schema (`TaskType`), Reference Scoping (`ReferenceScopeType`)
- **Affected Iterations**: `20260825-transparent-proxy` → `20260825-cred-manager-profiles`

#### Description

DogdouSpec supports `scope="project"` in `<ref>` elements, but the schema only permits it inside
`RefListType`. There is no enforced or even documented convention for a task in one iteration to
formally express a dependency on a deliverable from a *previous* iteration. This gap materializes
concretely in this workspace.

#### Evidence

`20260825-transparent-proxy/tasks.xml` — task `20260825-task-proxy-config` implements `config_parser.hpp/cpp`:

```xml
<scope>
  <repository path="src/">
    <include path="include/dogdouclix/config_parser.hpp"/>
    <include path="src/lib/config_parser.cpp"/>
  </repository>
</scope>
```

`20260825-cred-manager-profiles/tasks.xml` — task `20260825-task-cred-config` reuses the same files:

```xml
<scope>
  <repository path="src/">
    <include path="include/dogdouclix/config_parser.hpp"/>   <!-- same file -->
    <include path="src/lib/config_parser.cpp"/>              <!-- same file -->
    <include path="src/lib/forwarder.cpp"/>
  </repository>
</scope>
<origin>
  <ref scope="iteration" target="20260825-req-cred-config-reference" relation="implements"/>
  <!-- No reference to the proxy iteration's deliverable that owns config_parser -->
</origin>
```

`20260825-task-cred-config` has no `<dependencies>` referencing `20260825-task-proxy-config` or
deliverable `20260825-deliv-proxy-config` across the iteration boundary.

#### Impact

- If `config_parser.hpp` is refactored, split, or superseded in a future iteration, there is no
  automated way to discover that `cred-manager-profiles` tasks depend on it.
- Cross-iteration impact analysis requires manual reading of all task scope declarations — it cannot
  be expressed as a DogdouSpec query.

#### Upstream Recommendation

- Define a first-class convention (and document it in `SKILL.md`) for expressing cross-iteration
  deliverable dependencies using `scope="project"` references, e.g.:
  ```xml
  <dependencies>
    <ref scope="project" target="20260825-deliv-proxy-config" relation="depends-on"/>
  </dependencies>
  ```
- Alternatively, introduce a `knowledge.xml` entry type for "shared component boundary" that tasks
  can reference, making cross-iteration coupling explicit without requiring a full inter-iteration
  dependency graph.

---

### ISSUE-009: Task Scope Paths Are Declared but Never Validated Against Repository State

- **Date Logged**: 2026-08-25
- **Severity**: Medium (Scope Integrity / Auditability)
- **Component**: `validate` command, Task Schema (`RepositoryScopeType`)
- **Affected Iterations**: All four

#### Description

Every task declares a `<scope><repository>` with `<include path="..."/>` glob patterns specifying
which files the task is permitted to modify. The `validate` command performs schema and semantic
validation but does **not** verify that declared paths exist in the repository or that actual code
changes (as recorded by `git diff`) fall within the declared scope.

#### Evidence

Task `20260825-task-cred-cli` (`20260825-cred-manager-profiles/tasks.xml`) declares:

```xml
<scope>
  <repository path="src/">
    <include path="src/cli/main.cpp"/>
    <include path="src/lib/forwarder.cpp"/>
  </repository>
</scope>
```

However, a C++ feature touching `main.cpp` will almost certainly involve corresponding header files
(`include/dogdouclix/forwarder.hpp`, `include/dogdouclix/cred_manager.hpp`) that are absent from
the scope declaration. Additionally, task `20260825-task-repo-bootstrap` declares:

```xml
<include path="src/**"/>   <!-- Overly broad — no meaningful constraint -->
```

Running `.\dogdouspec.cmd validate --format xml` returns `valid="true"` in all cases regardless of
path accuracy, because path validation is not implemented.

#### Impact

- Scope declarations are documentation-only and provide false confidence that task boundaries are
  enforced.
- Agents may declare narrow scopes to appear disciplined, while actual code changes exceed boundaries —
  with no automated detection.

#### Upstream Recommendation

- Add an optional `--verify-scope <task-id> [--git-ref HEAD~1]` sub-command to `task` that diffs
  the actual changed files against declared includes/excludes and reports violations as warnings.
- Discourage overly broad patterns like `src/**` by emitting a validation hint when detected.

---

### ISSUE-010: `design_snapshot` Field Is Defined in Schema but Never Populated

- **Date Logged**: 2026-08-25
- **Severity**: Medium (Agent Continuity / Context Recovery)
- **Component**: Task Schema (`TaskContextType`), `task update` `context_update`
- **Affected Iterations**: All four (all 17 tasks)

#### Description

`tasks.xsd` defines `<context>` as:

```xml
<xs:complexType name="TaskContextType">
  <xs:sequence>
    <xs:element name="summary" type="xs:string"/>
    <xs:element name="key_points" type="KeyPointsType" minOccurs="0"/>
    <xs:element name="design_snapshot" type="xs:string" minOccurs="0"/>
  </xs:sequence>
</xs:complexType>
```

`design_snapshot` is explicitly designed to carry a snapshot of the implementation's key architectural
decisions, interface contracts, and data structures as of the `verify` or `complete` transition —
allowing a resuming agent to recover context without re-reading the entire codebase.

#### Evidence

All 17 task `<context>` elements across the workspace contain only a one-line `<summary>`:

```xml
<!-- 20260825-cli-forwarding/tasks.xml, task 20260825-task-stream-streaming -->
<context>
  <summary>Process execution and I/O forwarding engine.</summary>
  <!-- design_snapshot: absent -->
</context>

<!-- 20260825-transparent-proxy/tasks.xml, task 20260825-task-proxy-resolver -->
<context>
  <summary>Target resolution subsystem.</summary>
  <!-- design_snapshot: absent -->
</context>
```

The `requests.xsd` `TaskContextUpdateType` exposes a `<design_snapshot>` field for mutations,
but no task `complete` or `verify` update in the workspace has ever used it.

#### Impact

- If an agent needs to `resume` an `in-progress` task or a human developer needs to understand the
  implementation approach of a completed task, the only DogdouSpec-internal source of information is
  a single-sentence summary. All architectural context is implicit in the code.
- This negates a significant portion of the value of storing structured task context in DogdouSpec.

#### Upstream Recommendation

- Add guidance in `SKILL.md` (under "Step 3: Verify Task") mandating that `context_update` at
  `verify` time MUST populate `<design_snapshot>` with: key interfaces/data structures introduced,
  test coverage summary, and any notable implementation constraints discovered.
- Consider making `design_snapshot` mandatory (remove `minOccurs="0"`) for tasks where
  `transition="complete"`.

---

### ISSUE-011: Iteration IDs Lack Sub-Day Resolution, Causing Ambiguous Ordering

- **Date Logged**: 2026-08-25
- **Severity**: Low (Tooling / External Integration)
- **Component**: Iteration ID Convention (`TimeFirstIdType`)
- **Affected Scope**: All iterations created on the same calendar day

#### Description

`TimeFirstIdType` allows either `YYYYMMDD-slug` or `YYYYMMDDTHHmmssZ-slug` format. All four
iterations in this workspace used the `YYYYMMDD-` prefix:

```
20260825-cli-forwarding       created_at="2026-08-25T01:14:09Z"
20260825-transparent-proxy    created_at="2026-08-25T01:38:09Z"
20260825-cred-manager-profiles created_at="2026-08-25T01:53:24Z"
20260825-config-scaffolding    created_at="2026-08-25T02:08:15Z"
```

The IDs are lexicographically sorted by slug (`cli-forwarding` < `config-scaffolding` < `cred-manager-profiles` < `transparent-proxy`),
which does not match chronological order (`cli-forwarding` → `transparent-proxy` → `cred-manager-profiles` → `config-scaffolding`).

#### Evidence

Running `.\dogdouspec.cmd iteration list --format xml` produces the four iterations in an order
determined by directory enumeration, not by `created_at`. No `created_at` attribute is surfaced in
the `<iteration>` summary element of the list output, making temporal ordering invisible without
loading each `spec.xml` individually.

#### Impact

- Changelog generators, CI dashboards, and reports based on iteration ID ordering will misrepresent
  the actual development sequence.
- Human reviewers inferring order from directory listing (`dir .dogdouspec`) will see alphabetical
  order, not chronological.

#### Upstream Recommendation

- Prefer the `YYYYMMDDTHHmmssZ-slug` format for iteration IDs when multiple iterations may be
  created on the same day. This naturally sorts iterations in creation order.
- Add `created_at` to the `<iteration>` summary element in `iteration list` output to make
  temporal ordering queryable without loading individual spec documents.

---

### ISSUE-012: Replanning Workflow Has Never Been Exercised — Critical Path Unverified

- **Date Logged**: 2026-08-25
- **Severity**: Medium (Risk / Operational Readiness)
- **Component**: Replanning Subsystem (`change propose`, `iteration confirm --replan`, `change apply`)
- **Affected Scope**: All iterations (none triggered the replanning path)

#### Description

DogdouSpec's replanning workflow (`change propose` → owner `replan` confirmation →
execution freeze → `change apply` → owner `continue` confirmation) is the most complex path in the
system and the one most critical for handling real-world scope surprises. It has not been triggered
in any of the four iterations in this workspace.

#### Evidence

Reviewing all four `spec.xml` confirmation logs:

- `20260825-cli-forwarding/spec.xml`: Only `activate` and `complete` confirmations. No `replan`.
- `20260825-transparent-proxy/spec.xml`: Only `activate` and `complete` confirmations. No `replan`.
- `20260825-cred-manager-profiles/spec.xml`: Only `activate` and `complete` confirmations. No `replan`.
- `20260825-config-scaffolding/spec.xml`: Only `activate` and `complete` confirmations. No `replan`.

No task in any iteration has `@status="blocked"`, `@status="superseded"`, or `@status="transferred"`.
No `change-propose` operation records exist in any task's `<records>`.

#### Impact

- The CLI commands `change propose` and `change apply`, as well as the `replan`/`continue`
  `iteration confirm` actions, are entirely untested against real iteration state in this repo.
- If a genuine mid-flight scope surprise occurs in a future active iteration, agents would be exercising an unfamiliar path under pressure.

#### Upstream Recommendation

- Create a documented "replanning smoke test" procedure in `SKILL.md` or a dedicated `AGENTS.md`
  checklist: a minimal synthetic scenario that exercises `change propose` → `replan` → `change apply`
  → `continue` against a scratch iteration, verifiable end-to-end.
- Record the results of any such drill in `.agents/MEMORY.md`.

---

### ISSUE-013: `<design>` (ADR) Node Defined in Schema but Unused in All Iterations

- **Date Logged**: 2026-08-25
- **Severity**: Medium (Architectural Traceability)
- **Component**: `spec.xsd` (`DesignType`, `DesignDecisionType`), `iteration confirm` (`accept-design-change`)
- **Affected Iterations**: All four

#### Description

`spec.xsd` defines an optional `<design>` block inside `<iteration>` for recording Architecture
Decision Records (ADRs) with structured boundaries and decisions:

```xml
<xs:element name="design" type="DesignType" minOccurs="0"/>
```

`DesignType` contains `<boundaries>` and `<decisions>` elements, each with lifecycle status
(`proposed`, `accepted`, `rejected`, `superseded`). The `ConfirmationActionType` includes
`accept-design-change` as a dedicated confirmation action, showing this was designed for owner-gated
architectural decisions. No iteration has ever used this node.

#### Evidence

All four `spec.xml` files terminate with `<confirmations>` immediately after `</product>`, with
no `<design>` block:

```xml
<!-- 20260825-cli-forwarding/spec.xml (abridged) -->
<iteration id="20260825-cli-forwarding" ...>
  <index>...</index>
  <product>...</product>
  <!-- No <design> block -->
  <confirmations>...</confirmations>
</iteration>
```

Significant architectural decisions made during these iterations include:

- **Why Win32 `CreateProcessW` + manual handle inheritance** over .NET `Process` (Windows-native
  binary fidelity, no .NET runtime dependency) — documented only in task `rationale` free text.
- **Dual JSON/INI config format** over a single format — mentioned in `spec.xml` scope description,
  never recorded as a formal decision with rationale and alternatives considered.
- **DPAPI-backed `CRED_TYPE_GENERIC`** storage strategy — appears only in requirement statements.

#### Impact

- Architecture decisions that span multiple iterations (e.g. the config format choice affects
  both `transparent-proxy` and `cred-manager-profiles`) cannot be queried or linked via DogdouSpec.
- Future iterations or contributors have no structured way to discover *why* key choices were made
  without reading all historical task rationale text.
- The `accept-design-change` confirmation action is dead code from a usage perspective.

#### Upstream Recommendation

- For architecturally significant decisions that cross iteration boundaries, mandate use of
  `<design><decisions><decision>` with an `accept-design-change` owner confirmation.
- Document when a design decision is "significant enough" to warrant the `<design>` block vs.
  simple task-level `<rationale>` text — provide a decision heuristic in `SKILL.md`.

---

### ISSUE-014: Lack of First-Class Issue & Bug Tracking Entity (Workspace / Iteration Level)

- **Date Logged**: 2026-08-25
- **Severity**: High (Feature / Process Architecture Gap)
- **Component**: Core Specification Architecture (`.dogdouspec/` schema, CLI commands)
- **Affected Scope**: Workspace-wide defect management & cross-iteration tracking

#### Description

DogdouSpec is heavily iteration- and task-centric, structured around `spec.xml` and `tasks.xml`. However, it lacks an independent `issue` entity or centralized `issues.xml`.

In real-world development, defects, regression bugs, operational friction points, and technical debt emerge asynchronously across iterations or post-release. Without a first-class issue tracker:
1. Developers and agents are forced to maintain out-of-band, unstructured markdown files (e.g. `docs/dogdouspec_issues.md`).
2. Issues cannot be queried via DogdouSpec's XPath projections (`ds:filter`), validated by schema rules, or tracked through formal state transitions.
3. Minor defect fixes require either creating full-fledged Feature Iterations or stuffing unrelated bugfixes into existing feature tasks, distorting iteration scope.

#### Impact

- **Defect Disconnection**: Out-of-band issue logs cannot be referenced as `<origin>` targets by tasks in subsequent iterations without manual text copying.
- **No Traceability to Closure**: There is no automated state machine link to transition an issue from `open` → `in-progress` → `resolved` when its resolving task is marked `done`.

#### Upstream Recommendation

1. **Workspace Issue Store**: Introduce `.dogdouspec/issues.xml` with schema:
   ```xml
   <issues workspace="...">
     <issue id="ISSUE-001" severity="critical|major|minor" status="open|triaged|in-progress|resolved|closed|wont-fix">
       <title>...</title>
       <description>...</description>
       <repro_steps>...</repro_steps>
       <origin_iteration ref="20260825-transparent-proxy" />
       <resolution task_ref="20260825-task-fix-path" />
     </issue>
   </issues>
   ```
2. **CLI Commands**: Provide `dogdouspec issue create`, `dogdouspec issue list`, `dogdouspec issue get`, `dogdouspec issue resolve`.
3. **Task Linkage**: Support `dogdouspec task add --origin issue:ISSUE-001` or `dogdouspec task quick --origin issue:ISSUE-001` to automatically link and close issues upon task completion.

---

### ISSUE-015: Lack of Formalized Multi-Role Code Review & Audit Gate Workflow

- **Date Logged**: 2026-08-25
- **Severity**: High (Quality Assurance / Multi-Agent Governance)
- **Component**: Task State Machine & Quality Gate (`tasks.xsd`, `task update`)
- **Affected Scope**: Task verification lifecycle across all iterations

#### Description

The task lifecycle defines an intermediate `verification` status (`pending` → `in-progress` → `verification` → `done`). However, the transition from `verification` to `done` currently lacks a formal **Review Gate (审查门禁)**:

1. **Single-Agent Self-Approval Risk**: The implementing agent can transition a task to `verify` and immediately call `complete` (`done`) in the same turn, effectively "self-coding, self-testing, and self-approving" without independent audit.
2. **Disconnected Review Artifacts**: Code reviews generated by specialized review agents (e.g. `.reviews/` markdown files produced by review skills) exist outside the DogdouSpec XML state machine. DogdouSpec cannot enforce that `CRITICAL` or `OPEN` findings block task completion.

#### Impact

- **Gating Blind Spot**: `iteration readiness` checks only that tasks are in terminal states and criteria are reported passed, but cannot verify whether an independent reviewer signed off on code quality, security, or architecture rules.
- **Review Finding Leakage**: Review findings left in `.reviews/*.md` are not machine-enforced by `dogdouspec validate`.

#### Upstream Recommendation

1. **Role Segregation on Task Completion**: Restrict `task update --transition complete` to require an `approved` review record (`<record kind="review" status="approved">`) signed by a designated reviewer or owner actor.
2. **Structured Review CLI**: Introduce `dogdouspec task review submit --task <ID> --status changes-requested|approved --finding ...`.
   - If `status="changes-requested"`, the task automatically attaches active findings and reverts to `in-progress` or `blocked`.
   - If `status="approved"`, the task is unlocked for `transition="complete"`.
3. **Readiness Invariant**: Update `iteration readiness` to verify that all completed tasks have valid, non-self-signed `approved` review records.

---

## Usage Observations & Workflow Summary

1. **Deterministic Orchestration**: DogdouSpec XML state machines provided rigorous gating between technical task completion and product owner decisions.
2. **Fast Validation**: `.\dogdouspec.cmd validate --format xml` verified XML schema and semantic constraints in <50ms.
3. **Isolation Success**: The entire DogdouSpec system remained completely uncommitted and decoupled from Git commits, fulfilling the repository isolation rule.
4. **Full Traceability Chain Confirmed**: Every task carries `<origin>` links to iteration requirements, and every completion record has `<covers>` links to task criteria — the `Requirement → Task → Criterion` chain is machine-readable and intact.
5. **Authority Gate Functioned Correctly**: All completed iterations strictly required explicit product owner activation and completion confirmations, reliably preventing unapproved feature closeouts.
6. **Unused Schema Capacity**: `<design>` (ADR), `<design_snapshot>` (context recovery), `<knowledge>` (shared facts), and `<backlog>` (deferred work) represent significant built-in capability that was never exercised across four feature iterations — suggesting the schema is more capable than the current agent workflow guidance activates.