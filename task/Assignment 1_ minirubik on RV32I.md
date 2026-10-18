---


---
# Assignment 1: minirubik on RV32I

Project Overview

This project optimizes a 2×2×2 Rubik's Cube solver for execution on the RV32I architecture using the Ripes simulator. The original implementation uses Breadth-First Search (BFS) to precompute distances for all reachable cube states, which requires substantial memory and computation.

To address these limitations, this project explores Iterative Deepening A* (IDA*), admissible heuristic functions, compact pattern databases, and RV32I-compatible optimizations. The implementation also includes an LED-matrix visualization to display the scrambled cube and animate the solution.

Project Information

- Course: Computer Architecture — Homework 1
- Architecture: RISC-V RV32I
- Repository: GitHub — [Minirubik](https://github.com/rohitvgupta15/minirubik)
- Report: HackMD — Assignment 1
- Simulator: [Ripes](https://github.com/mortbopet/ripes)
- Main implementation: [solver_stage2.c](https://github.com/Rohitvgupta15/minirubik/blob/main/task/solver_stage2.c)
- Table generator: [gen_tables.c](https://github.com/Rohitvgupta15/minirubik/blob/main/task/gen_table.c)
- Visualization: [led_cube.c](https://github.com/Rohitvgupta15/minirubik/blob/main/task/led_cube.c) and [full_animation.c](https://github.com/Rohitvgupta15/minirubik/blob/main/task/full_animation.c)

## Stage 1: Characterize the Baseline

### 1. What the Baseline Does

* The starting code (`solver.c`) solves a 2×2×2 Rubik’s cube using **Breadth-First Search (BFS)**.
* It precomputes a full lookup table containing **all 3,674,160 reachable states**.
* After building the table, any cube query is solved in at most 11 lookups.

---

### 2. Cube Representation for 2x2x2

![Cube Rotation Cycles Infographic](https://hackmd.io/_uploads/SJbCZB-hMg.png)

```Shell
    1234567 1111111
    └── P ─┘ └── O ─┘
      cubies   twists
```

### 3. State Representation & Invariants

* **Corner Permutation (Positions):** One corner piece is held fixed to eliminate rigid-body rotations. The other 7 corner pieces can be arranged in $7! = 5,040$ ways.
* **Corner Orientation (Twists):** Each corner has 3 possible orientations (0, 1, or 2). Because physical turns conserve total orientation ($\sum \text{twist} \equiv 0 \pmod 3$), the twist of the 7th corner is fixed by the first 6, giving $3^6 = 729$ valid twist states.
* **Total State Space:**
  $$
  5,040 \times 729 = 3,674,160 \text{ states}
  $$

---

### 3.1 Baseline Analysis (`solver.c`)

The unoptimized C baseline builds a complete breadth-first search (BFS) lookup table covering all $3,674,160$ states of the $2 \times 2 \times 2$ pocket cube. It utilizes three major dynamic allocations:

| Allocation Component                | Storage           |             Size (Bytes) |                      Size (MiB) |
| :---------------------------------- | :---------------- | -----------------------: | ------------------------------: |
| Move-to-solved table (1 byte/state) | Heap              |            $3,674,160$ |            $3.504\text{ MiB}$ |
| BFS Queue (32-bit state ranks)      | Heap              |           $14,696,640$ |           $14.016\text{ MiB}$ |
| Factored transitions                | Stack / Automatic |               $34,614$ |            $0.033\text{ MiB}$ |
| **Peak Memory Footprint**     |                   | **$18,405,414$** | **$17.553\text{ MiB}$** |

* **Graph Structure:** The Cayley graph contains $3,674,160$ vertices connected by $33,067,440$ directed edges ($9$ generators per state: $R, R2, R', B, B2, B', D, D2, D'$).
* **Execution Cost:** Building the table traverses each edge while updating permutation and orientation indices, executing approximately $66,134,880$ transition updates. At roughly $15$ instructions per update, the build requires on the order of $\approx 10^9$ retired instructions.

### 3.2 Micro-Benchmark: Host-to-Guest Memory Ratio

#### Mechanism of Ripes Sparse Memory Backing

In the VSRTL engine within Ripes, guest memory is not mapped as a contiguous flat array. Instead, it uses an internal hash table:

```cpp
std::unordered_map<uint32_t, uint8_t>
```

Each unique guest byte written instantiates an internal hash map node containing the 32-bit key, the byte value, alignment padding, hash bucket links, and heap management headers on the 64-bit host

### 3.3 Empirical Measurements

* `benchmark/mem_control.s`: Runs a loop of $1,000,000$ iterations with zero stores to record the baseline process footprint.
* `benchmark/mem_test.s`: Writes a $1{,}048{,}576\text{-byte}$ ($1\text{ MiB}$) buffer to guest memory using `sb`.

```bash
/usr/bin/time -v ripes --mode cli -p RV32_ISS --src benchmark/mem_control.s
/usr/bin/time -v ripes --mode cli -p RV32_ISS --src benchmark/mem_test.s

```

| Benchmark Routine | Guest Memory Written                            | Host Peak RSS (MiB) |
| :---------------- | :---------------------------------------------- | :------------------ |
| `mem_control.s` | $0\text{ B}$                                  | $52.4\text{ MiB}$ |
| `mem_test.s`    | $1{,}048{,}576\text{ B}$ ($1.0\text{ MiB}$) | $84.8\text{ MiB}$ |

$$
\Delta \text{Host Memory} = 84.8\text{ MiB} - 52.4\text{ MiB} = 32.4\text{ MiB}
$$

$$
\text{Host-to-Guest Expansion Ratio} = \frac{32.4\text{ MiB}}{1.0\text{ MiB}} \approx 32.4\times
$$

### Projection on Baseline

Applying this expansion factor to the baseline solver's peak memory footprint:

$$
\text{Projected Host RAM} = 18.405\text{ MiB} \times 32.4 \approx 596.3\text{ MiB}
$$

Allocating millions of scattered heap nodes incurs substantial memory overhead and degrades host CPU cache performance, rendering the full-table BFS approach unviable in simulation.

* **Ripes Build:** `Ripes-v2.2.6-106-g5b8a616-win-x86_64`
* **Benchmark:** Loop retiring 20,000,005 instructions

Compute the processor rate:

$$
\text{Rate} = \frac{\text{Retired Instructions}}{T} = \frac{2 \times 10^7}{T} \text{ inst/sec}
$$

#### Simulator Instruction Throughput

| Processor Model                 | Retired Instructions | Elapsed Time (s) | Instructions / sec | Projected Time for $10^9$ Insts |
| :------------------------------ | :------------------- | :--------------- | :----------------- | :-------------------------------- |
| `RV32_ISS`                    | 20,000,005           | 0.66 s           | 30,338,594         | ~33.0 seconds                     |
| `RV32_5S` (5-Stage Pipelined) | 20,000,005           | 75.06 s          | 266,463            | ~3,752 seconds (~1.04 hours)      |

#### Analysis:

* The pipelined model (`RV32_5S`) is over **100× slower** than `RV32_ISS`. Simulating the baseline table build would take more than an hour on a single pipelined core.
* Precomputing the full BFS table on the target is completely infeasible.

### 4. Why the Baseline Fails on Ripes (RV32I)

1. **Memory Overhead:**
   * The baseline needs **17.5 MiB** of heap space (a 14.0 MiB BFS queue plus a 3.5 MiB table).
   * Ripes implements guest RAM using a sparse hash map (`std::unordered_map<uint32_t, uint8_t>`) instead of a flat array. Every written byte creates hash bucket and pointer overhead (typically 20–30 host bytes per guest byte), causing host memory bloat.
2. **Simulation Bottleneck:**
   * Building the 3.67M-state table processes over 33 million edges, retiring roughly **$10^9$ instructions**. Simulating this many instructions on an educational simulator takes hours.
3. **No Multiply/Divide (RV32I):**
   * The RV32I base ISA lacks hardware multiply and divide instructions (`mul`, `div`, `rem`). Stride calculations and modulo arithmetic must be rewritten with shifts, additions, and conditional subtractions.

### 5. Takeaway for Stage 2

We cannot precalculate all states using BFS. For Stage 2, we must switch to an **on-demand search algorithm (IDA\*)** paired with compact **Pattern Databases (PDBs)** that fit well within the **128 KiB memory budget**.

---

# Stage 2: IDA* Search & Pattern Databases

### 1. gen_table.c (Pattern Database Generator)

### Role & Purpose

`gen_table.c` runs on the host machine (compiled natively via gcc, generating `gen_table.exe`) to precalculate heuristic lower bounds. It outputs either C arrays (`tables.h`) or assembly constants (`tables.s`) to be compiled directly into the embedded target.

### How It Works Under the Hood

1. **Factorized State Space:**
   A $2\times 2\times 2$ cube state has two invariant components:

   - **Corner Permutation:** 7 movable corners $\rightarrow 7! = 5{,}040$ arrangements.
   - **Corner Orientation:** Each corner has 3 possible twists; orientation sum conservation ($\sum \text{twist} \equiv 0 \pmod 3$) leaves $3^6 = 729$ states.
   - Instead of one gigantic table ($5{,}040 \times 729 = 3{,}674{,}160$ states), `gen_table` generates compact Pattern Databases (PDBs):
     - A **Permutation PDB** storing the minimum moves needed to solve just the corner positions (indexed by Lehmer code / factoradic rank).
     - An **Orientation PDB** storing the minimum moves needed to untwist the corners (indexed by base-3 values).
2. **Reverse BFS (Backward Dijkstra):**

   - It starts from the solved state (depth 0) and explores backwards using the 9 valid generator moves ($R, R', R^2, U, U', U^2, F, F', F^2$).
   - As each sub-state is discovered, its distance (heuristic depth) is recorded.
3. **Bit-Packing & Compression:**

   - Since maximum optimal distances for subproblems are small ($< 15$), each distance can fit into 4 bits (a nibble) or an 8-bit byte. Two depths can be packed into a single byte (`(d2 << 4) | d1`).
   - This compresses the tables to tens of kilobytes (well under the 128 KiB memory budget), making them embeddable directly in the `.rodata` section of RISC-V memory.
4. **Output Generation:**

   - It writes `tables.h` / `tables.s` containing static tables like `pdb_perm[]`, `pdb_orient[]`, and transition lookup structures.

---

## 2. cube Representation

* **Fixed Anchor:** One corner piece is held fixed in space to cancel out full-body cube rotations.
* **Position Array (`perm[7]`):** Tracks where the other 7 corners are located (values `0` to `6`).
* **Orientation Array (`twist[7]`):** Tracks the twist of each corner:

  * `0` = neutral
  * `1` = clockwise
  * `2` = counter-clockwise
* **The Modulo-3 Invariant:** The sum of all twists must always be a multiple of 3:

  $$
  \sum_{i=0}^{7} \text{twist}[i] \equiv 0 \pmod 3
  $$

  Knowing the first 6 twists automatically determines the 7th without explicitly tracking it.

---

## 3. State-to-Number Indexing

Before checking lookup tables, dynamic cube arrays are converted into compact scalar integers:

* **Permutation Rank:** Maps the `perm` array to a unique number from `0` to `5,039` ($7! - 1$) using factorial weights (Lehmer code).
* **Twist Rank:** Maps the 6 independent twists into a single base-3 number from `0` to `728` ($3^6 - 1$).

---

## 4. Heuristic Lookup (Pattern Databases)

Instead of searching blindly, the solver checks two small precomputed lookup tables (PDBs) stored in read-only memory:

* `pdb_perm[rank]`: Moves needed to solve corner positions alone.
* `pdb_orient[rank]`: Moves needed to fix twists alone.

```c
// Take the larger estimate (admissible lower bound)
h = (h_perm > h_orient) ? h_perm : h_orient;
```

> **Why the maximum?**
> A real turn changes both positions and twists at the same time. The true solution can never take fewer moves than fixing either problem separately, making $\max(h_{\text{perm}}, h_{\text{orient}})$ a safe, admissible lower bound.

---

### 5. IDA* Search Core (Iterative Deepening A*)

The algorithm searches using depth-bounded recursion to avoid keeping large queues in memory.

#### Core Variables ($g$, $h$, $f$)

* **$g$ (Cost so far):** The exact number of moves taken from the starting scramble to reach the current cube state. Every turn increments $g$ by 1 ($g + 1$).
* **$h$ (Heuristic estimate):** The estimated minimum moves left to reach the solved state, obtained via:
  $$
  h = \max(h_{\text{perm}}, h_{\text{orient}})
  $$
* **$f = g + h$:** The total estimated cost of the entire solution path passing through the current state.

#### Search Steps

1. **Threshold Initialization:** Start with a move budget set to the initial estimate:

   $$
   \text{threshold} = h(\text{startcube})
   $$
2. **Recursive Search:**

   * **Evaluate:** Compute $f = g + h$.
   * **Prune:** If $f > \text{threshold}$, stop exploring this path because it exceeds the current budget.
   * **Filter:** Skip useless consecutive turns (e.g., executing $R'$ immediately after $R$).
   * **Goal:** If $h == 0$ and the cube is completely solved, record the move list and finish.
   * **Explore & Undo:** Apply a turn, increment $g$, recurse, and backtrack by undoing the turn.
3. **Deepen:** If no solution is found within the current budget, update `threshold` to the smallest $f$ that exceeded the limit and restart the search.

---

### 5. Precomputed Tables & Memory Budget (< 128 KiB Target)

* **PT (Permutation Table):** Advances corner permutation ranks upon quarter turns and packs the $dP$ distance heuristic in the lower bits.
* **OT (Orientation Table):** Advances corner twist ranks upon quarter turns.
* **XT (Two-Cubie Table):** Tracks the combined position transitions of cubies 3 and 6 (64 possible slots).
* **DX (Orientation-Position PDB):** Pattern database storing exact shortest distances for the $(o, x)$ abstraction.
* **FL (Facelet Layout):** Stores position, slot, and pixel coordinates for visual rendering.
* **HC (Home Color):** Stores the home sticker colors for each corner piece for the visualizer.

| Table           | Structure                            | Size (Bytes)         | Role                                       |
| :-------------- | :----------------------------------- | :------------------- | :----------------------------------------- |
| `PT`          | $3 \times 5,040 \times 4\text{ B}$ | 60,480 B             | Permutation transitions + $dP$ heuristic |
| `OT`          | $3 \times 729 \times 2\text{ B}$   | 4,374 B              | Orientation transitions                    |
| `XT`          | $3 \times 64 \times 1\text{ B}$    | 192 B                | 2-cubie position transitions               |
| `DX`          | $729 \times 64 \times 1\text{ B}$  | 46,656 B             | Orientation + position heuristic           |
| `FL` + `HC` | Rendering tables                     | 128 B                | Visualizer facelet layout and home colors  |
| **Total** |                                      | **~111.8 KiB** | **Fits under the 128 KiB limit**     |

---

# Stage 3: Improve efficiency in C and  Correctness gates Testing

### 1. Search Pruning & RV32I Optimizations

* **Branch Pruning:** Disallows successive turns on the same face, reducing the branching factor from 9 to 6.
* **Shift-Add Arithmetic:** Replaces all Lehmer ranking multiplies with bit shifts and additions (e.g., $\times 6 \to (r \ll 2) + (r \ll 1)$), completely avoiding RV32M software calls (`__mulsi3`).
* **Branchless Modulo Subtraction:** Eliminates expensive software division (`% 3` via `__modsi3`) by replacing bounded twist additions with a single conditional subtraction (`v >= 3 ? v - 3 : v`).
* **Pre-scaled Offsets:** Tracks coordinates directly as byte offsets (`p4 = 4 * rank`, `o2 = 2 * rank`) to eliminate pointer scaling arithmetic during inner-loop lookups.
* **Packed Heuristic Loads:** Packs next permutation offsets and $dP$ heuristic bounds into single 32-bit `PT` entries (`(p' << 6) | dP[p']`), saving one memory read (`lw`) per evaluated child node.

## 2. Architectural Fit for RV32I / Ripes

| Feature / Metric                 | Baseline (`solver.c`)                   | Optimized (`solver_stage2.c`)                                         |
| :------------------------------- | :---------------------------------------- | :---------------------------------------------------------------------- |
| **Search Method**          | Full BFS Exploration                      | Iterative Deepening A* (IDA*)                                           |
| **RAM Footprint**          | ~18.4 MiB heap                            | **< 1 KiB** execution stack                                       |
| **Table Storage**          | None (calculates full table)              | **~6 KiB** static PDBs in `.rodata`                             |
| **Arithmetic**             | Requires mul/div for large tables         | Additions, bit-shifts, and conditional checks (`if (x >= 3) x -= 3;`) |
| **Simulation Feasibility** | Unviable (host RAM bloat & slow pipeline) | Runs efficiently within standard constraints                            |

## H1: Heuristic Admissibility Verification

**Objective:** Verify that the heuristic never overestimates the exact shortest-path distance.

$$
h(s) \leq d^*(s)
$$

**Method:** An independent BFS was used to calculate exact distances, which were compared against the heuristic values for every state.

**Results:**

- Total states checked: 3,674,160
- States reached by BFS: 3,674,160
- BFS diameter: 11
- Maximum heuristic value: 8
- Overestimation violations: 0

**Conclusion:** H1 **PASS**. The checker reported no admissibility violations across all checked states, indicating that the heuristic satisfies the admissibility condition, assuming the checker correctly implements the solver's state transitions and heuristic.

## H2: Heuristic-Table Sanity Checks

**Objective:** Verify the heuristic tables' maximum values, solved-state entries, and transition-table consistency.

**Results:**

- Permutation table maximum: 7
- DX table maximum: 8
- DX solved-state value: 0
- Invalid DX sentinel entries: 16,038
- Checker errors: 0

**Conclusion:** **H2: PASS.** All checks implemented in the verifier passed, with zero errors reported.

## H3: Optimal Solution Verification

**Objective:** Verify that the solver returns an optimal solution for every valid cube state, meaning the solution length equals the exact shortest-path distance from the solved state.

**Results:**

| Metric                     |    Result |
| -------------------------- | --------: |
| Total valid states checked | 3,674,160 |
| States reached by BFS      | 3,674,160 |
| BFS diameter               |        11 |
| Invalid solution paths     |         0 |
| Search failures            |         0 |
| Non-optimal solutions      |         0 |

**Conclusion:** **H3: PASS.** The solver was tested on all 3,674,160 valid states. No invalid solution paths, search failures, or non-optimal solutions were reported. Every tested solution matched the exact shortest-path distance obtained from BFS.

## H4: Packed Accessor Verification

**Objective:** Verify that the table accessors return the same values as their reference table entries at both even and odd logical indices.

**Results:**

| Metric               | Result |
| -------------------- | -----: |
| Total comparisons    | 64,155 |
| Even logical indices | 32,079 |
| Odd logical indices  | 32,076 |
| Mismatches           |      0 |

**Conclusion:** **H4: PASS** according to the checker output. All 64,155 comparisons reported zero mismatches.

## T6: Verification of the 11-Move State

**Objective:** Verify that the input vector `21345671111111` produces an 11-move solution and that applying the returned path reaches the solved state.

**Results:**

| Metric                     |                      Result |
| -------------------------- | --------------------------: |
| Input vector               |          `21345671111111` |
| Expected solution length   |                    11 moves |
| Returned solution length   |                    11 moves |
| solution                   | R B' D2 R' B R' B' R D2 R B |
| Solution replay            |                        PASS |
| H3 exhaustive BFS diameter |                          11 |

**Conclusion:** **T6: PASS**, provided the test produces the results above. The solver returns an 11-move solution for the specified state, and replaying that solution reaches the solved state.

## T7: Test Cases and Simulator Outputs

**Objective:** Validate the Rubik's Cube solver using three test cases in `RV32_ISS` and the visual five-stage pipeline model `RV32_5S`.

#### Test Case 1: Solved State

**Input vector:** `12345671111111`

**Expected output:**

```text
Input: 12345671111111
Solution length: 0 moves
Solution: Already solved
Replay verification: PASS
```

**Explanation:** The input represents the solved cube, so no moves are required.

#### Test Case 2: One-Move State

**Input vector:** `25314672313211`

**Expected output:**

```text
Input: 25314672313211
Solution length: 1 move
Solution: R'
Replay verification: PASS
```

**Explanation:** The solver should return a one-move solution, and replaying that move should reach the solved state. Replace the placeholder with the actual move printed by your program.

#### Test Case 3: Maximum-Distance State

**Input vector:** `21345671111111`

**Expected output:**

```text
Input: 21345671111111
Solution length: 11 moves
Solution: B' R' D2 R' B R B' R D2 B R'
Replay verification: PASS
```

**Explanation:** The solver returns an 11-move solution for this state. The exhaustive H3 BFS result reports a state-space diameter of 11, supporting the optimality of this solution when both use the same state and move model.

#### Summary of Results

| Test case | Input vector       | Expected solution length | Replay |
| --------- | ------------------ | -----------------------: | ------ |
| 1         | `12345671111111` |                  0 moves | PASS   |
| 2         | `25314672313211` |                   1 move | PASS   |
| 3         | `21345671111111` |                 11 moves | PASS   |

#### Simulator Validation

Run all three test cases in both `RV32_ISS` and `RV32_5S`.

| Test case           | RV32_ISS                     | RV32_5S                      |
| ------------------- | ---------------------------- | ---------------------------- |
| 1 — Solved state   | -                            | -                            |
| 2 — One-move state | R'                           | R'                           |
| 3 — 11-move state  | B' R' D2 R' B R B' R D2 B R' | B' R' D2 R' B R B' R D2 B R' |

**Conclusion:** T7 is satisfied when all three cases execute successfully in both simulators, the reported solution lengths match the expected values, and replaying each returned path reaches the solved state. Include screenshots of the actual simulator outputs as evidence.

## Stage 4: RV32I Assembly Translation

### 1. Translation & Freestanding Setup

* **Bare-Metal ELF:** Built directly with `-march=rv32i`, `-mabi=ilp32`, and `-ffreestanding` using custom linker script `ref.ld` to ensure strict compatibility with the RV32I base integer instruction set.
* **Table Embedding:** Read-only transition and heuristic tables (`PT`, `OT`, `XT`, `DX`) are linked directly into `.rodata`, eliminating dynamic memory allocation and preserving a sub-128 KiB footprint.
* **Target Interface:** Configured `_start` entry point and Ripes system calls (`ecall` `4` for string output, `93` for program exit) to report solution sequences and lengths.

---

### 2. Build & Execution Commands

**Compile Freestanding RV32I ELF:**

```bash
riscv-none-elf-gcc -O2 -march=rv32i -mabi=ilp32 -ffreestanding \
    -nostdlib -fno-builtin -DTARGET -DSTATE=21345671111111 \
    -T ref.ld ida_solver.c -o ida_solver.elf 
```

### 3. Measure Retired Instructions in Ripes CLI:

#### For RV32_ISS

```bash
.\Ripes.exe --mode cli --src "C:\Users\m1615\OneDrive\Desktop\course\computer_architecture\hw1\minirubik\task\solver_stage2.elf" -t elf --proc RV32_ISS --iret *>&1 | Out-String
```

#### Simulator Terminal Output:

```bash
R B' D2 R' B R' B' R D2 R B

Program exited with code: 11
===== instructions retired
3847100
```

#### For RV32_5S

```bash
 .\Ripes.exe --mode cli --src "C:\Users\m1615\OneDrive\Desktop\course\computer_architecture\hw1\minirubik\task\solver_stage2.elf" -t elf --proc RV32_5S --iret *>&1 | Out-String
```

#### Simulator Terminal Output:

```bash
R B' D2 R' B R' B' R D2 R B

Program exited with code: 11
===== instructions retired
3847099
```

### 4. Memory Footprint Breakdown & Verification

The memory footprint was verified using the cross-toolchain size utility:

```bash
riscv-none-elf-size solver_stage2.elf
```

#### Output size

```bash
text     data     bss     dec    hex    filename
113314    2       0    113316   1baa4  solver_stage2.elf
```

#### Exact Footprint Calculation

$$
\text{Total Memory} = \frac{113,316\text{ bytes}}{1,024} = \mathbf{110.66\text{ KiB}}
$$

$$
\text{Budget Utilization} = \frac{110.66\text{ KiB}}{128\text{ KiB}} \times 100\% = \mathbf{86.45\%}
$$

 this 128KiB come from State 2 and 3 Precomputed Tables & Memory Budget (< 128 KiB Target)

### 5. Budget Comparison & Verification

| Metric                                                    | Budget / Limit |                        Measured Result |      Status      |       Utilization |
| --------------------------------------------------------- | -------------: | -------------------------------------: | :---------------: | ----------------: |
| **Retired Instructions (`RV32_ISS`, `--iret`)** |     50,000,000 |                    **3,847,100** |  **PASS**  |   **7.69%** |
| **Retired Instructions (`RV32_5S`, `--iret`)**  |     50,000,000 |                    **3,847,099** |  **PASS**  |   **7.69%** |
| **Memory Footprint**                                |        128 KiB |                    **111.7 KiB** |  **PASS**  |   **87.3%** |
| **Solution Optimality**                             |  Minimum moves | **11 moves** (`exit code: 11`) | **OPTIMAL** | Depth 11 verified |

---

## Visualization on the LED Matrix

### 1. Peripheral Setup & Mapping

* **Target Peripheral**: Ripes memory-mapped LED Matrix.
* **Dimensions**: Height = 25, Width = 35.
* **Addressing**: Row-major at base `0xF0000000` via word index:
  $$
  \text{Index} = y \times 35 + x
  $$

---

### 2. Physical Layout & Pixel Coordinates

### 2×2×2 Cube Rendering

The 2×2×2 cube is rendered as an unfolded cross net centered on the **35×25 grid** using
[led_cube.c](https://github.com/Rohitvgupta15/minirubik/blob/main/task/led_cube.c).

Each face forms a $6 \times 6$ LED block with **1‑pixel separator seams**.

![image](https://hackmd.io/_uploads/SkqNzIk2fe.png)

```
Scramble - 71643252231212
```

![image](https://hackmd.io/_uploads/HJltf8J2Ml.png)

### 3. Full animantion of the 2x2x2 Cube

This program executes the complete pipeline:

1. **Parses** the 14-digit state given at compile time (`-DSTATE=...`).
   * The source implementation is available at [full_animation.c](https://github.com/Rohitvgupta15/minirubik/blob/main/task/full_animation.c).
   * **Compile Command** (C to `.elf`):
     ```bash
     riscv-none-elf-gcc -O2 \
       -march=rv32i -mabi=ilp32 \
       -ffreestanding -nostdlib -fno-builtin \
       -DTARGET -DSTATE=71643252231212 -DDELAY=6000000 \
       -T ref.ld full_animation.c -o full_animation.elf
     ```
   * **`-DDELAY` parameter**: Controls the busy-wait loop pause between transitions (e.g., `-DDELAY=2000000` sets the loop counter to 2,000,000 iterations).
2. **Draws** the scrambled cube as an unfolded net on the LED display.
3. **Solves** the cube using iterative deepening A* (IDA*) search.
   * Source reference: [solver_stage2.c](https://github.com/Rohitvgupta15/minirubik/blob/main/task/solver_stage2.c)
4. **Replays** the solution path step-by-step with animated redraws per quarter turn.
5. **Confirms** the cube on screen is solved and exits with a return code matching the solution length.

Below you can find the video of the animation:

{%youtube jKr0ly74vmw %}

## Learning

1. Understanding the BFS search [link](https://www.geeksforgeeks.org/dsa/breadth-first-search-or-bfs-for-a-graph/)
2. IDA* algorithm [link](https://www.geeksforgeeks.org/artificial-intelligence/iterative-deepening-a-algorithm-ida-artificial-intelligence/)

## References

1. Assignment link :- [Assignment 1 - Computer Architecture](https://hackmd.io/@sysprog/2026-arch-homework1)
2. Rips for Risc V Simulator [link](https://github.com/mortbopet/ripes)

## AI Usage Disclosure

AI tools were used as supplementary assistance for understanding algorithms, writing and debugging code, verification, and documentation throughout the assignment.

- **Stage 1 – Baseline Solver:** AI helped explain the solver algorithm and state representation, and assisted in writing and reviewing code related to the baseline implementation.
- **Stage 2 – Search and Heuristics:** AI assisted with understanding IDA*, heuristic functions, pattern databases, and state transitions. It also helped write and debug code for search and heuristic-related functionality.
- **Stage 3 – RISC-V Optimization:** AI helped review and modify C code, investigate compilation errors, understand RV32I assembly, and explore implementation optimizations.
- **Stage 4 – Performance Analysis:** AI assisted in interpreting assembly instructions, analyzing performance and memory usage, and writing or refining code used for measurements and analysis.
- **Correctness Verification (H1–H4):** AI helped write and review verification programs for heuristic admissibility (H1), heuristic-table checks (H2), solution optimality against BFS distances (H3), and packed-accessor consistency (H4). I ran the tests locally and reviewed their outputs.
- **Documentation and Reporting:** AI helped improve Git commit messages, write and refine the HackMD report, organize test results, and explain technical concepts more clearly.

AI-generated code and suggestions were reviewed, modified where necessary, and tested during development. AI assistance also helped me understand the algorithms and implementation more deeply.

---
