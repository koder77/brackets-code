# Brackets-Code Generator

A CLI tool for generating Brackets (L1VM) code from natural language prompts.

## Installation

```bash
# Clone and build
git clone https://github.com/koder77/brackets-code.git
cd brackets-code
make

# Build the training tool (optional, for neural prompt classifier)
make train_tiny
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

# Train the tiny transformer (optional)
make train
./train_tiny --model tiny_model.tiny --predict "your prompt"
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

## Tiny Transformer (Neural Prompt Classifier)

brackets-code includes a tiny transformer neural network (~34K parameters) that learns
to classify natural language prompts into the correct code emitter. It runs entirely
in C with no external dependencies.

### How It Works

```
Prompt → Tokenize → Embedding (32d) → Transformer (1 Layer, 2 Heads) → Softmax → 166 Emitters
```

The transformer is trained on all 162 DSL rules. It learns to map prompts like
"fibonacci 10" to the `fib_seq` emitter, "bubble sort" to `bubble_sort`, etc.
When a trained model (`tiny_model.tiny`) exists, it is used automatically.
If no model is found, the system falls back to the existing keyword-based scoring.

### Training

Train the transformer on your DSL rules:

```bash
# Build the training tool
make train_tiny

# Train (default: 50 epochs, lr=0.005)
make train

# Or with custom parameters
./train_tiny --dsl-dir dsl --model tiny_model.tiny --epochs 100 --lr 0.001
```

Training takes about 30-60 seconds and produces two files:
- `tiny_model.tiny` — model weights (~136 KB)
- `tiny_model.tiny.vocab` — vocabulary mapping

Example output:
```
Loaded 162 DSL rules, 789 training examples, 469 vocab words
Training tiny transformer: 469 vocab, 32 embed_dim, 1 layers, 2 heads
Parameters: ~34K
Epoch  50/50  loss=0.5004  accuracy=82.8%
Model saved to tiny_model.tiny
```

### Predicting

Test the trained model on arbitrary prompts:

```bash
./train_tiny --model tiny_model.tiny --predict "fibonacci 10"
./train_tiny --model tiny_model.tiny --predict "sort array"
./train_tiny --model tiny_model.tiny --predict "hello name"
```

Output:
```
Prompt: "fibonacci 10"
Predicted emitter: fib_seq (confidence: 63.09%)

Prompt: "bubble sort"
Predicted emitter: bubble_sort (confidence: 86.22%)

Prompt: "fizzbuzz"
Predicted emitter: fizzbuzz (confidence: 94.79%)
```

### Integration with brackets-code

Once trained, the transformer is used automatically:

```bash
# Just use brackets-code as usual — the transformer runs in the background
brackets-code "fibonacci 10"
brackets-code "fizzbuzz"

# Verbose mode shows transformer predictions
brackets-code --verbose "bubble sort"
```

The system uses the transformer when confidence > 50%, otherwise falls back
to the rule-based keyword scoring. This ensures reliability even if the
model is uncertain.

### Architecture

| Parameter | Value |
|---|---|
| Vocab size | ~470 words |
| Embedding dim | 32 |
| Transformer layers | 2 |
| Attention heads | 2 |
| Hidden dim | 64 |
| Total parameters | ~46K |
| Model file size | ~180 KB |

### Files

| File | Description |
|---|---|
| `tiny_transformer.h` | Header: structures, configuration, API |
| `tiny_transformer.c` | Implementation: matrix ops, transformer, training |
| `train_tiny.c` | Standalone training/prediction tool |
| `code_patterns.h` | Header: code pattern database, parameter extraction |
| `code_patterns.c` | Implementation: pattern loading, code generation |
| `tiny_model.tiny` | Trained model weights (generated) |
| `tiny_model.tiny.vocab` | Vocabulary mapping (generated) |

## Hybrid Code Generation

brackets-code uses a hybrid approach for code generation:

```
Prompt → Transformer (classification) → Emitter ID
         ↓
     Variation Selection (recursive/iterative/simple)
         ↓
     Parameter Extraction (numbers/strings from prompt)
         ↓
     Code Pattern Database (DSL-based templates)
         ↓
     Generated L1VM Code
```

### How It Works

1. **Transformer Classification**: The tiny transformer classifies the prompt into one of 166 emitters
2. **Variation Selection**: Keywords like "recursive", "simple", "optimized" select code variations
3. **Parameter Extraction**: Numbers and strings are extracted from the prompt (e.g., "fibonacci 10" → n=10)
4. **Pattern Selection**: The best code pattern is selected based on the emitter and extracted parameters
5. **Code Generation**: The pattern template is filled with extracted parameters to produce L1VM code

### Example

```bash
brackets-code "fibonacci 10"
```

Flow:
1. Transformer: "fibonacci 10" → emitter `fib_seq` (91% confidence)
2. Variation: "iterative" (default)
3. Extraction: numbers=[10], strings=[]
4. Pattern: `fib_seq` pattern with `token: int64 n`
5. Code: Generated L1VM code with n=10

### Code Pattern Database

The pattern database includes 11 hand-crafted patterns with multiple variations:

| Pattern | Emitter | Variations |
|---|---|---|
| fibonacci | fib_seq | iterative, recursive |
| bubble_sort | bubble_sort | ascending, descending |
| hello_name | hello_name | simple, newline |
| fizzbuzz | fizzbuzz | standard, compact |
| factorial | factorial | iterative, recursive |
| primes | primes | simple, sieve |
| selection_sort | selection_sort | ascending, descending |
| calculator | calculator | basic, full |
| hello_world | hello_world | simple, newline |
| string_length | string_length | simple |
| array_reverse | array_reverse | simple |

Each pattern stores code templates with parameter placeholders:

```c
CPCodePattern pattern = {
    .id = "fibonacci",
    .emitter_id = 7,  // fib_seq
    .params = {{ .name = "n", .type = CP_PARAM_INT }},
    .variations = {
        { .name = "iterative", .complexity = 0,
          .code_lines = { "(set const-int64 1 zero 0)", ... } },
        { .name = "recursive", .complexity = 2,
          .code_lines = { "(function fib {n} =", ... } }
    },
    .num_variations = 2
};
```

### Variation Selection

Prompts can specify which code variation to use:

| Prompt | Variation |
|---|---|
| `"fibonacci 10"` | iterative (default) |
| `"recursive fibonacci"` | recursive |
| `"simple fizzbuzz"` | standard |
| `"optimized sort"` | optimized |
| `"descending sort"` | descending |
| `"basic calculator"` | basic |

### Multi-Step Generation

Complex prompts are split into steps:

```bash
brackets-code "sort numbers then print"
```

Output:
```
Split steps: num_steps=2 steps: [0]='sort numbers' [1]='print'
Step 1/2: sort numbers
  transformer prediction: selection_sort (68.00%)
Step 2/2: print
  transformer prediction: math (0.60%)
Written: sort_numbers_then_print.l1com
```

### Neural Parameter Extractor

The neural extractor is trainable on prompt→extraction pairs:

```c
/* Create extractor */
CPExtractor *ext = cp_extractor_create();

/* Add training examples */
CPExtraction target = { .num_params = 1,
    .params = { { .name = "n", .type = CP_PARAM_INT } } };
cp_extractor_add_example("fibonacci 10", &target);
cp_extractor_add_example("factorial 5", &target);

/* Train */
cp_extractor_train(ext, &vocab, 100, 0.01);

/* Use for extraction */
CPExtraction extraction;
cp_extractor_extract(ext, &vocab, "fibonacci 10", &extraction);
// extraction.num_params = 1, extraction.params[0].type = CP_PARAM_INT
```

The extractor uses a simple feed-forward network:
- Input: 32-dimensional prompt embedding
- Hidden: 16 neurons with ReLU
- Output: 16 values (8 params × 2 values each)

### Training Results

| Task | Before | After |
|---|---|---|
| Pattern DB | 4 patterns | 11 patterns |
| Variations | 2-4 per pattern | 2-4 per pattern |
| Transformer | 77% accuracy | 72% accuracy (more examples) |
| Neural Extractor | Regex only | Trainable neural network |

## Neural Enhancements

brackets-code includes several neural network components for intelligent code generation:

### 1. Code Embeddings

Semantic embeddings for code patterns enable similarity search:

```c
CodeEmbedder *ce = code_emb_create();
code_emb_add_pattern(ce, "fibonacci", "(set const-int64 1 zero 0)...");
code_emb_add_pattern(ce, "factorial", "(set const-int64 1 result 1)...");

float scores[10];
int top = code_emb_find_similar(ce, "fib seq", scores, 5);
// Returns patterns ranked by similarity to prompt
```

### 2. Attention Pattern Selector

Multi-head attention mechanism for pattern selection:

```c
AttentionSelector *sel = attn_sel_create(num_patterns);
Matrix *prompt_emb = ...;  /* From transformer */
float scores[128];
int best_idx;
attn_sel_predict(sel, prompt_emb, scores, &best_idx);
// Uses Q/K/V attention to select best pattern
```

### 3. Prompt Expander

Neural paraphrase generation for prompt augmentation:

```c
PromptExpander *pe = expander_create(vocab_size, embed_dim);
char paraphrase[256];
expander_paraphrase(pe, "fibonacci 10", paraphrase, 256);
// "fibonacci 10" → "fibonacci sequence 10"
```

### 4. Reinforcement Learning Agent

Q-learning agent for optimizing code selection:

```c
RLAgent *agent = rl_agent_create(num_states, num_actions, 0.1, 0.99);
int action = rl_agent_choose_action(agent, state);
rl_agent_learn(agent, state, action, reward, next_state);
rl_agent_save(agent, "rl_agent.bin");
// Learns from code execution feedback
```

### Architecture

```
Prompt → Transformer (classification)
         ↓
     Code Embeddings (similarity search)
         ↓
     Attention Selector (Q/K/V)
         ↓
     RL Agent (feedback loop)
         ↓
     Pattern Selection
         ↓
     Code Generation
```

### Parameter Extraction

The extractor pulls structured data from prompts:

| Prompt | Numbers | Strings |
|---|---|---|
| `"fibonacci 10"` | [10] | [] |
| `"sort 5 numbers"` | [5] | [] |
| `"hello \"World\""` | [] | ["World"] |
| `"add 3 and 7"` | [3, 7] | [] |

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
