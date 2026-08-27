# C++ Code Generation Rules

Applies to: `**/*.cc`, `**/*.h`, `**/*.c`, `**/*.cpp`, `**/*.hpp`.

## Naming Conventions & Structures (Strictly Enforced)

* Classes, Functions, and Methods: MUST use `PascalCase` (e.g., `MyClass`, `CalculateValue`).
* Function Parameters: MUST use `PascalCase` (e.g., `InputData`).
* Class Member Variables: MUST use `_PascalCase` with a leading underscore (e.g., `_MemberVar`).
* Struct Definitions: STRICTLY use the typedef pattern with `UPPER_SNAKE_CASE`. Example: `typedef struct _MY_DATA_STRUCT { ... } MY_DATA_STRUCT;`
* Struct Members: MUST use `PascalCase` without a leading underscore (e.g., `StructField`).
* Local Variables: MUST use flat `lowercase` with zero underscores. NEVER use camelCase or snake_case here.
* Constants: MUST be defined using `constexpr` or `const` and named in `PascalCase` (e.g., `constexpr int MaxSize = 100;`).
* Global Variables: MUST use `PascalCase` with a leading underscore (e.g., `_GlobalVar`), but global variables should be avoided whenever possible.
* In file scope functions: MUST be declared as `static` to limit their visibility to the translation unit, and follow the same naming convention as other functions (e.g., `static void HelperFunction();`).

## Class Layout Order (Strictly Enforced)

When generating or refactoring classes, you MUST adhere to the following exact layout sequence:

* 1st - Friends: MUST declare `friend` classes and functions at the absolute top of the class, before any access modifiers.
* 2nd - Access Modifiers: MUST be ordered strictly as `public`, then `protected`, then `private`.
* 3rd - Internal Block Order: Within EACH access modifier block, you MUST declare members in this exact top-to-bottom sequence:
  1. Types and Aliases (`typedef`, `using`, `enum`, nested `struct`/`class`).
  2. Constants (`const`, `constexpr`).
  3. Methods (Regular member functions).
  4. Destructors and Constructors. CRITICAL: You MUST explicitly restate the current access modifier (e.g., `public:`, `protected:`, or `private:`) immediately before the Destructor. Then, you MUST place the Destructor FIRST, followed by all Constructors.
  5. Data Members (Member variables). These MUST ALWAYS be at the absolute bottom of their respective block.

## Formatting & Encoding (Strictly Enforced)

* Indentation: You MUST ALWAYS use exactly 2 spaces. Tab characters are STRICTLY PROHIBITED.
* Line Endings: Enforce LF (`\n`) only.
* Whitespace: STRICTLY NO trailing whitespace.
* Encoding: Use ASCII characters exclusively. NEVER include non-ASCII characters in code, strings, or comments.

## Comments & Documentation

* Language: English only.
* Detail Level: Every non-trivial piece of code MUST be accompanied by clear comments. Required coverage includes:
  * Core design decisions and the rationale behind architectural choices.
  * Important logic: any flow whose intent is not immediately obvious from reading the code.
  * Implementation techniques: clever or non-standard patterns (e.g., lock-free tricks, WFP API ordering constraints, RPC lifecycle quirks).
  * Complex context: any place where understanding depends on external contracts, driver model rules, or protocol state that is not visible locally.
* Trivial code (simple assignments, obvious getters, boilerplate) does NOT require comments.

## Debug Assertions

* Use the project `ASSERTF` macro (defined in `include/dogdouclix/common.hpp`) for all in-code debug-time invariant checks. Never use the CRT `assert()` macro or raw `__debugbreak()` for invariant checks.
* `ASSERTF` is active only in `_DEBUG`/`DBG` builds and is a no-op in release builds. It is safe to place liberally without any performance concern.
* Assertion placement rules:
  * Assert preconditions at the top of non-trivial functions (e.g., pointer validity, handle state, expected range).
  * Assert postconditions and loop invariants where intermediate state must be verified.
  * Assert unreachable branches (e.g., `ASSERTF(0)` in default cases of exhaustive switches).
* Provide an optional message argument to `ASSERTF` (`ASSERTF(exp, "context message")`) for any assertion whose failure might not be self-explanatory in a debugger.
