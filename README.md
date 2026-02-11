# Postflop-Poker-Solver
![Build Status](https://github.com/kfg021/Postflop-Poker-Solver/actions/workflows/ci.yml/badge.svg)

This project is a high performance solver that computes Game Theory Optimal (GTO) poker strategies using Counterfactual Regret Minimization (CFR). As a command line application, it provides an interactive prompt for defining scenarios, solving game trees, and exploring optimal strategies. The solver is capable of handling complex Texas Hold'em postflop situations and also includes simplified variants like Kuhn and Leduc Poker, which are useful for testing and educational purposes.

## Features
- **Discounted CFR Algorithm**: Uses the state-of-the-art DCFR algorithm with optimized parameters ($\alpha=1.5$, $\beta=0$, $\gamma=2$) for fast convergence
- **Parallel Processing**: Multi-threaded solving with OpenMP support for significant speedups on multi-core systems
- **Suit Isomorphism**: Exploits suit symmetries to drastically reduce tree size and memory usage
- **Optimized C++ Engine**: A highly optimized core delivering competitive solving speeds and excellent memory efficiency.
- **Interactive CLI**: Explore solved game trees, view strategies for specific hands, and navigate through decision points
- **Flexible Configuration**: YAML-based configuration files for defining custom Hold'em scenarios
- **Multiple Game Support**:
  - Texas Hold'em (postflop scenarios starting from flop, turn, or river)
  - Leduc Poker (simplified two-round poker)
  - Kuhn Poker (minimal poker variant for algorithm verification)

## Prerequisites
- A C++20 compatible compiler
- CMake 3.14 or higher
- Git

## Building

The project is written in C++20 and uses [yaml-cpp](https://github.com/jbeder/yaml-cpp) for configuration parsing, [Google Test](https://github.com/google/googletest) for unit testing, and [replxx](https://github.com/AmokHuginnsson/replxx) to enhance the command line interface. Dependencies are fetched automatically via CMake.

```bash
# Clone the repository
git clone https://github.com/kfg021/Postflop-Poker-Solver.git
cd Postflop-Poker-Solver

# Configure and build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run tests (optional)
ctest --test-dir build -C Debug --output-on-failure
```

## Usage

Run the solver:

```bash
./build/PostflopSolver
```

```
PostflopSolver 1.0.0
Type "help" for more information.
> 
```

### CLI Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `holdem` | `<file>` | Load Hold'em settings from a YAML configuration file |
| `kuhn` | - | Load Kuhn Poker (3 cards, 1 betting round) |
| `leduc` | - | Load Leduc Poker (6 cards, 2 betting rounds) |
| `size` | - | Estimate game tree size and memory requirements |
| `solve` | - | Solve the game tree using Discounted CFR |
| `info` | - | Display information about the current node |
| `strategy` | `<hand-class>` | Show optimal strategy for a hand class (e.g., `AA`, `AKo`, `JTs`), or `all` for the entire range |
| `action` | `<id>` | Take an action at a decision node |
| `deal` | `<card>` | Deal a card at a chance node (e.g., `Td`) |
| `back` | - | Return to the parent node |
| `root` | - | Return to the root of the game tree |
| `help` | - | Display available commands |
| `exit` | - | Exit the solver |

## Configuration File Format

Hold'em scenarios are configured using YAML files. See `examples/` for complete examples.

```yaml
# Player ranges 
# OOP = Out of Position (first player to act)
# IP = In Position (second player to act)
ranges:
  oop: "AA, KK, QQ, AK, AQs, A5s"
  ip: "QQ, JJ, TT, 99, AKo, AQ, AJs, ATs, KQs, KJs, KTs, QJs, JTs, T9s"

# Starting board (3-5 cards)
board: "9s, 8h, 3s"

# Game tree configuration
tree:
  starting-wager-per-player: 50   # The amount each active player has already contributed to the pot. Note that this value is half the total starting pot traditionally used in solver configurations.
  dead-money-in-pot: 0            # Money in the pot from players who have already folded.
  effective-stack-remaining: 100  # The smallest remaining stack size for an active player.
  use-isomorphism: true           # Enable suit isomorphism optimization.
  
  # Action configuration for each street.
  # Bet and raise sizes are expressed as a percentage of the current pot size.
  actions:
    oop:
      flop:
        bet-sizes: [33, 150]  # Allows bets of 33% and 150% of the pot.
        raise-sizes: [50]     # Allows raises of 50% of the pot.
      turn:
        bet-sizes: [33, 150]
        raise-sizes: [50]
      river:
        bet-sizes: [33, 150]
        raise-sizes: [50]
    ip:
      flop:
        bet-sizes: [33, 150]
        raise-sizes: [50]
      turn:
        bet-sizes: [33, 150]
        raise-sizes: [50]
      river:
        bet-sizes: [33, 150]
        raise-sizes: [50]

# Solver parameters
solver:
  threads: 6                          # Number of threads for parallel solving (requires OpenMP).
  target-exploitability-percent: 0.3  # Stop when exploitability is less than this percentage of the starting pot. Lower values lead to more accurate solutions but increase solve time.
  max-iterations: 1000                # The solver will stop after this many iterations, even if target exploitability is not reached.
  exploitability-check-frequency: 10  # Check exploitability every n iterations.
```

### Range Syntax

Ranges use standard poker notation:

| Notation | Meaning |
|----------|---------|
| `AA` | Pocket aces (all combos) |
| `AKs` | Ace-king suited (all suited combos) |
| `AKo` | Ace-king offsuit (all offsuit combos) |
| `AK` | Ace-king (all combos, suited and offsuit) |
| `A5s:0.5` | Ace-five suited at 50% frequency |

**Note**: Range shortcuts like `TT+` or `22-55` are currently not supported. You must list each combo explicitly (e.g., `TT,JJ,QQ,KK,AA` instead of `TT+`).

## Typical Workflow

A common session involves loading a configuration, solving the resulting tree, and then interactively exploring the resulting strategy.

1.  **Start the solver:**
    ```bash
    ./build/PostflopSolver
    ```

2.  **Load a configuration file:**
    ```
    > holdem your-config-file.yml
    ```
    The `flop-4bet-pot-1spr.yml` example has a low stack-to-pot ratio and narrow hand ranges, leading to a smaller game tree that solves quickly. The `flop-3bet-pot-large-spr.yml` example is much larger and will take significantly longer to solve.

3.  **Estimate the size of the game tree:**
    ```
    > size
    ```
    Run this command before solving to ensure that the game tree will fit in your system's RAM.

4.  **Solve the game:**
    ```
    > solve
    ```
    The solver will periodically print its progress until the target exploitability is reached.

5.  **Explore the solution:**

    After solving, you are placed at the root of the tree, and information about the current node is automatically displayed. Below is an example of what you may see:
    ```
    Node type: Decision
    Board: 9s 8h 3s
    OOP wager: 50
    IP wager: 50
    Total pot size: 100
    Player to act: OOP
        [0] Check
        [1] Bet 50
        [2] All-in 100
    ```
    You can run the `info` command at any time to reprint this information. The solver's interface is based on moving between different **Node Types**.

    ---

    ### Decision Nodes
    A Decision Node is a point where a player must choose an action. In the example above, it is OOP's turn, and they have three options.

    To see the GTO strategy at this node, use the `strategy` command:
    ```
    > strategy all
    +------+---------+-------+-------+-------+
    | Hand | Weight  | [0]   | [1]   | [2]   |
    +------+---------+-------+-------+-------+
    | AsAh | 1.000   | 0.027 | 0.244 | 0.729 |
    ...
    +------+---------+-------+-------+-------+
    | all  | 42.000  | 0.022 | 0.316 | 0.662 |
    +------+---------+-------+-------+-------+
    ```
    This table shows the optimal proportion for each action. In this example, with `AsAh`, the optimal strategy is to `Check` 2.7% of the time, `Bet 50` 24.4% of the time, and `All-in` 72.9% of the time. The `Weight` column represents how frequently a hand reaches this specific node given the optimal strategy on previous actions.

    You can also view the strategies for specific hand classes:
    ```
    > strategy AA
    > strategy AKo
    > strategy JTs
    ```
    To move down the tree, use `action <id>`, where `<id>` is the number of the action. To simulate OOP betting 50:
    ```
    > action 1
    ```

    ### Chance Nodes
    A Chance Node represents a random event, like the dealing of the turn or river card.
    ```
    Node type: Chance
    Board: 9s 8h 3s
    OOP wager: 100
    IP wager: 100
    Total pot size: 200
    Possible cards: 2c 2d 2h 2s 3c 3d 3h ... Ac Ad Ah As
    ```
    `Possible cards` shows all cards that could legally come next. To move the game forward, use the `deal` command with a specific card from this list. For example, to deal the ten of diamonds:
    ```
    > deal Td
    ```

    ### Terminal Nodes (Fold & Showdown)
    These nodes represent the end of a hand.
    - A **Fold Node** occurs when one player folds, and the other wins the pot.
    - A **Showdown Node** occurs when the final betting round is complete, and players must reveal their hands to determine a winner.

    At these terminal nodes, no more actions can be taken. You can use `info` to review the final pot size, board, and outcome.

    ### Navigating the Tree
    At any node, you can use the following commands to move around the tree:
    - `back`: Returns to the parent of the current node.
    - `root`: Instantly jumps back to the very first node of the game tree.

## Algorithm

The solver implements **Discounted Counterfactual Regret Minimization (DCFR)** with parameters $\alpha = 1.5$, $\beta = 0$, $\gamma = 2$, which were shown to provide excellent convergence in practice ([Brown & Sandholm, 2019](https://doi.org/10.1609/aaai.v33i01.33011829)).

## Optimizations

The solver computes exact solutions without any hand abstractions or bucketing. Several optimizations are employed to maximize solving speed and minimize memory usage:

- **Suit Isomorphism**: Strategically equivalent subtrees (differing only by suit permutations) are collapsed into a single representative. For example, on a flop of `Qs Jh 2h`, dealing the `5c` or `5d` leads to equivalent game states since neither suit is present on the board. This is a lossless optimization that can reduce tree size by over 3x on certain board textures.

- **$O(n)$ Showdown Evaluation**: At showdown nodes, expected values are computed in linear time by iterating through both players' hands sorted by strength, using inclusion-exclusion to handle card removal effects. This avoids the naive $O(n^2)$ approach of comparing every hand combination.

- **Custom Stack Allocator**: CFR traversal requires many temporary arrays for reach probabilities and expected values. A custom stack allocator provides fast memory reuse within each thread, resulting in zero heap allocations during solving.

- **Task-Based Parallelism**: OpenMP tasks are spawned at chance and decision nodes, allowing independent subtrees to be processed in parallel across multiple threads.

- **Bitwise Operations**: Card sets and board states are represented as 64-bit integers, enabling fast set operations (intersection, union, population count) via bitwise arithmetic.

- **Data-Oriented Design**: Hot loops are structured for cache efficiency, operating over contiguous arrays of hand data rather than pointer-chasing through object hierarchies.

## Verification and Benchmarks

### Kuhn Poker

Kuhn Poker is a minimal poker game with a 3-card deck (Jack, Queen, King), where each player is dealt one card and there is a single betting round. The game has a known Nash equilibrium with a first-player expected value of exactly $-\frac{1}{18}$. The solver converges to this value and produces strategies matching the [known optimal strategy](https://en.wikipedia.org/wiki/Kuhn_poker#Optimal_strategy).

### Leduc Poker

Leduc Poker is a simplified two-round poker game with a 6-card deck (two suits, three ranks: Jack, Queen, King). Each player is dealt one private card, followed by a betting round, then a single community card is dealt, followed by another betting round. Pairs beat high cards. The game's Nash equilibrium has a first-player expected value of $\approx -0.0856$ ([Lanctot et al., 2017](https://doi.org/10.48550/arXiv.1711.00832)). The solver matches this value with very low exploitability.

### Hold'em

This solver was benchmarked against the popular open-source solvers [WASM Postflop](https://wasm-postflop.pages.dev/) and [TexasSolver](https://github.com/bupticybee/TexasSolver). We also tested against the Rust engine behind WASM Postflop, [postflop-solver](https://github.com/b-inary/postflop-solver). All solves used the configuration found in [examples/flop-3bet-pot-large-spr.yml](examples/flop-3bet-pot-large-spr.yml).

*(Each solver has slightly different settings. To ensure a fair comparison, configurations were standardized by disabling optional features such as all-in thresholding, bet merging, and raise limits that are implemented differently / not implemented at all across solvers. Isomorphism was enabled since it does not change the structure of the game tree and all solvers being tested support it.)*

#### Performance

Solving on my Intel MacBookPro using 6 threads and 0.3% target exploitability:

| Solver           | Solve time (s) | Iterations | Iterations / s | Estimated tree size (GB)    | Actual process memory (GB) |
|------------------|----------------|------------|----------------|-----------------------------|----------------------------|
| This solver      | 89.79          | 170        | 1.89           | 1.44                        | 1.48                       |
| WASM Postflop    | 101.05         | 150        | 1.48           | 1.50                        | 1.59                       |  
| postflop-solver  | 79.67          | 150        | 1.88           | 1.51                        | 1.46                       |
| TexasSolver      | 238.94         | 141        | 0.59           | 2.90                        | 2.04                       |

*(Performance results will vary based on hardware, operating system, and compiler.)*

The results confirm the solver's high-performance design, placing it firmly among modern, competitive open-source solutions. It demonstrates a strong overall profile, delivering fast solving speeds while maintaining excellent memory efficiency. This makes it a robust and practical tool for tackling complex game trees.

**Notes and observations:**
* The variation in the number of iterations required to converge is due to differing implementations and parameter choices within the DCFR algorithm.
* WASM Postflop runs about 20% slower than postflop-solver despite using the same engine, likely caused by the overhead of the WASM JIT compiler.
* WASM Postflop's performance is highly dependent on browser. The WASM test was performed in Chrome; other browsers like Firefox were up to 20% slower.
* *Estimated tree size* is a guess for the memory usage, reported by the solver itself. *Actual process memory* is the RAM usage of the running process, reported by the OS. TexasSolver significantly overestimates its memory usage, while the other three solvers provide much more accurate estimates.

#### Expected Value
* This solver outputs OOP and IP EVs of $0.86698$ and $-0.86698$, respectively:
  ```
  Calculating expected value of final strategy...
  Player 0 expected value: 0.86698
  Player 1 expected value: -0.86698
  ```

* WASM Postflop reports an OOP EV of $5.868$ and an IP EV of $4.132$. WASM Postflop's EVs are relative to the starting wager, so the comparable EVs are $5.868 - 5 = 0.868$ and $4.132 - 5 = -0.868$, which match this solver.

* TexasSolver does not report EV for its solves, but its strategy looks reasonable.

#### Strategies

To compare strategies, we investigate the node in the tree reached after OOP bets 10 (full pot bet) and IP raises to 25 (half pot raise).

It is OOP's turn to act, and they can either fold, call, raise to 55, or go all-in for 95.

This solver produces the following (truncated) output:

```
> info
Node type: Decision
Board: Qs Jh 2h
OOP wager: 15
IP wager: 30
Total pot size: 45
Player to act: OOP
    [0] Fold
    [1] Call
    [2] Raise 55
    [3] All-in 95
> strategy all
+------+---------+-------+-------+-------+-------+
| Hand | Weight  | [0]   | [1]   | [2]   | [3]   |
+------+---------+-------+-------+-------+-------+
| AsAh | 0.870   | 0.000 | 0.509 | 0.086 | 0.404 |
| AsAd | 1.000   | 0.000 | 0.008 | 0.289 | 0.703 |
...
+------+---------+-------+-------+-------+-------+
| all  | 105.733 | 0.401 | 0.379 | 0.050 | 0.170 |
+------+---------+-------+-------+-------+-------+
> 
```

Below is a comparison of the strategies found by each of the three solvers for all hands in OOP's range:

| Solver        | Weight | Fold% | Call% | Raise% | All-in% |
|---------------|--------|-------|-------|--------|---------|
| This solver   | 105.7  | 40.1  | 37.9  | 5.0    | 17.0    |
| WASM Postflop | 105.4  | 42.9  | 36.2  | 6.2    | 14.6    |
| TexasSolver   | 108.8  | 41.7  | 32.1  | 3.2    | 23.1    |

The strategies are similar, suggesting that all three converged to a similar solution.

## Future Work

- **GUI / Web Frontend**: Add a graphical user interface for easier setup and visualization of strategies.
- **Node Locking**: Allow fixing strategies at specific nodes to analyze exploitative play.
- **Performance Optimizations**: Increase performance by leveraging SIMD instructions in hot loops.
- **Half-Precision Floating-Point Storage**: Use 16-bit floats for regrets and strategies to cut memory usage in half.
- **Game Tree Enhancements**: Add support for rake, specific donk bet sizings, and automatic all-in/merging thresholds.

## References

This project was developed with reference to the following open-source solvers:

- **PostflopSolver**: [https://github.com/b-inary/postflop-solver](https://github.com/b-inary/postflop-solver) (The engine behind the popular **WASM Postflop** web interface)
- **TexasSolver**: [https://github.com/bupticybee/TexasSolver](https://github.com/bupticybee/TexasSolver)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.