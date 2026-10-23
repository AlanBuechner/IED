# Coding standards for this project

- **No global mutable state.** Pass what a function needs.
- **Honest functions:** a function touches the outside world (files, the console, the clock) only at the top level
  (`main`); the logic below it takes inputs and returns results, so it can be tested alone.
- **One level of abstraction per function.** A loop with a non-trivial body, or a commented step inside a function, is
  a missing function. Keep functions short: none longer than 40 lines.
- **Single purpose:** a function does one thing, a type models one concept. Organize code by concern into files
  (`.ch` declarations, `.cf` definitions); no grab-bag files.
- **No duplicated code.** Two copies of the same logic become one function.
- **No magic numbers.** Named constants say what a number means.
- **Use the library:** Bedrock's containers and algorithms (sorting, hash maps) instead of hand-written ones.
- **Tests:** every behavior of the core logic has unit tests under `src/tests/`, including edge cases.
- Comments are minimal: names explain the code.
