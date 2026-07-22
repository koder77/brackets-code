# Brackets-Code Generator

A CLI tool for generating Brackets (L1VM) code from natural language prompts.

## Installation

```bash
# Clone and build
git clone https://github.com/koder77/brackets-code.git
cd brackets-code
make

# Or with Makefile
./makefile
```

## Usage

```bash
# Interactive mode
brackets-code

# One-shot mode
brackets-code "your prompt here"

# With validation (requires l1pre/l1com)
brackets-code --validate "your prompt here"

# Self-test
brackets-code --self-test

# Batch mode
brackets-code --batch prompts.txt

# Vector search
brackets-code --search "your query"

# Learn new pattern
brackets-code --learn my_code.l1com "keyword1" "keyword2" "description"
```

## Flags

### Output Options
- `--output <dir>`: Output directory for generated files
- `--dry-run`: Print filename only, no output
- `--verbose`: Show emitter selection scores

### Validation
- `--validate <prompt>`: Run l1pre preprocessing and l1com compilation
- `--l1vm-root <path>`: L1VM installation root

## Template Examples

```bash
brackets-code "Sum of 1 to 100"
```

Generates:
```c
func sum() {
    int total = 0;
    for (int i = 1; i <= 100; i++) {
        total += i;
    }
    return total;
}
```

## Performance

Benchmark target: 197 code generation blocks → 12-15 seconds

Current optimizations:
- Template matching with keyword counting
- Early termination for simple prompts
- Vector search integration
- Learned pattern caching

## License

GPLv3 or later

## Extended .l1dsl File Format

The `.l1dsl` files are declarative rules that translate natural language into L1VM code.
In addition to the basic directives (`parser:`, `token:`, `code:`, etc.), 13 extended
directives are available to create more powerful and intelligent rules.

### Overview of All Directives

#### Basic Directives (existing)

| Directive | Description | Example |
|---|---|---|
| `parser:` | Keywords for prompt matching (comma-separated) | `parser: "fibonacci, fib, fib sequence"` |
| `token:` | Input variables with type | `token: int64 n, double x` |
| `result:` | Output variable | `result: double y` |
| `include:` | L1VM header file (pre-processing) | `include: intr-func.l1h` |
| `include-post:` | Header after main includes | `include-post: math-lib.l1h` |
| `var:` | Explicit variable declaration | `var: int64 myvar 1 42` |
| `desc:` | Human-readable description | `desc: "Computes Fibonacci number"` |
| `match:` | TaskProfile flags for exact matching | `match: has_fib_seq` |
| `array:` | Array rule declaration | `array: arr i int64` |
| `code:` | Start of code block | `code:` |

#### New Extended Directives

| Directive | Description | Example |
|---|---|---|
| `param:` | Rich parameters with validation | `param: int64 n "Count" min=1 max=100 default=10` |
| `require:` | External dependency (semantically strong) | `require: math-lib.l1h` |
| `example:` | Example prompts | `example: "fibonacci 10"` |
| `category:` | Hierarchical categorization | `category: math > sequences` |
| `version:` | Version tracking | `version: 2.0.0` |
| `complexity:` | Complexity level | `complexity: simple` |
| `alias:` | Additional aliases | `alias: "fibo, fib seq"` |
| `test:` | Built-in test cases | `test: "fibonacci 10" expect: "55"` |
| `help:` | Extended help text | `help: "Computes the nth Fibonacci number"` |
| `validate:` | Validation rules | `validate: no_division_by_zero` |
| `compose:` | Rule composition | `compose: base-rule, print-rule` |
| `init:` | Initialization code block | `init:` (before main code) |
| `cleanup:` | Cleanup code block | `cleanup:` (after main code) |

---

### Complete Example: fibonacci-smart.l1dsl

```l1dsl
// fibonacci-smart.l1dsl
parser: "fibonacci, fib, fib sequence"
alias: "fibo, fib seq, fibonacci number"
desc: "Computes the nth Fibonacci number iteratively"
category: math > sequences
version: 2.0.0
complexity: simple
help: "Computes the nth Fibonacci number iteratively. Parameter n specifies the number of iterations."
match: has_fib_seq
param: int64 n "Number of iterations" min=1 max=100 default=10
token: int64 n
result: int64 fib
include: intr-func.l1h
example: "fibonacci 10"
example: "fibonacci compute 20"
test: "fibonacci 10" expect: "55"

init:
    (set const-int64 1 zero 0)
    (set const-int64 1 one 1)

code:
    (set int64 1 a 0)
    (set int64 1 b 1)
    (set int64 1 i 2)
    (set int64 1 c 0)
    (set int64 1 f 0)
    (for-loop)
    (((i n <=) f :=) f for)
        (a + b c :=)
        (b a :=)
        (c b :=)
        (i + one i :=)
    (next)
    (b :print_i !)
    (:print_n !)

cleanup:
    (zero :exit !)
```

---

### Explanation of the New Directives

#### `param:` — Rich Parameter Definition

Extends `token:` with metadata for validation and user guidance.

```l1dsl
param: int64 n "Number of iterations" min=1 max=1000 default=10
param: string name "Your name" default="World" required
```

**Syntax:** `param: type name [desc="..."] [min=X] [max=X] [default=X] [pattern=regex] [required]`

| Attribute | Description |
|---|---|
| `desc=` | Parameter description |
| `min=` | Minimum value (int64/double) |
| `max=` | Maximum value (int64/double) |
| `default=` | Default value |
| `pattern=` | Regex pattern (string) |
| `required` | Parameter is mandatory |

---

#### `require:` — External Dependency

Like `include:`, but with semantic meaning: this file is strictly required.

```l1dsl
require: math-lib.l1h
require: fann-lib.l1h
```

---

#### `example:` — Example Prompts

Defines example prompts that should trigger this rule. Useful for
documentation and future matching.

```l1dsl
example: "fibonacci 10"
example: "compute fibonacci for 20"
example: "fib sequence 50"
```

---

#### `category:` — Hierarchical Categorization

Assigns the rule to a category using `>` separators.

```l1dsl
category: math > sequences
category: string > manipulation
category: data > sorting
```

---

#### `version:` — Version Tracking

Semantic versioning for rules.

```l1dsl
version: 2.1.0
```

---

#### `complexity:` — Complexity Level

Helps the system select the appropriate rule.

```l1dsl
complexity: simple     # Simple logic
complexity: medium     # Medium complexity
complexity: complex    # High complexity
```

---

#### `alias:` — Additional Aliases

Besides `parser:` for additional matching.

```l1dsl
alias: "fibo, fib seq, fibonacci number, fibonacci-sequence"
```

---

#### `test:` — Built-in Test Cases

Define test cases directly in the rule.

```l1dsl
test: "fibonacci 10" expect: "55"
test: "fibonacci 1" expect: "1"
test: "fibonacci 5" expect: "5"
```

---

#### `help:` — Extended Help Text

Detailed help displayed with `--help` or in the interactive shell.

```l1dsl
help: "Computes the nth Fibonacci number iteratively.\nUsage: fibonacci <number>\nExample: fibonacci 10"
```

---

#### `validate:` — Validation Rules

Named validation checks for the generated code.

```l1dsl
validate: no_division_by_zero, has_bounds_check
```

---

#### `compose:` — Rule Composition

Current rule extends other rules.

```l1dsl
compose: base-fibonacci, print-result
```

---

#### `init:` — Initialization Code

Code block executed **before** the main code (`code:`).

```l1dsl
init:
    (set const-int64 1 zero 0)
    (set const-int64 1 one 1)
    (zero :math_init !)
```

Useful for:
- Defining constants
- Initializing libraries
- Allocating resources

---

#### `cleanup:` — Cleanup Code

Code block executed **after** the main code (`code:`).

```l1dsl
cleanup:
    (zero :exit !)
```

Useful for:
- Releasing resources
- Closing connections
- Program cleanup

---

### Processing Order

Code generation follows this order:

```
1. include: / require:     ← Header files
2. include-post:           ← Post-processing headers
3. var:                    ← Variable declarations
4. token: / param:         ← Parameter variables
5. result:                 ← Result variable
6. init:                   ← Initialization code
7. code:                   ← Main code
8. cleanup:                ← Cleanup code
```

---

### Backward Compatibility

All 162 existing `.l1dsl` files in `dsl/` remain **completely unchanged**.
The new directives are optional — existing rules continue to work exactly as before.
