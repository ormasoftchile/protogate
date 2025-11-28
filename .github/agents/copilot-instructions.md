# Protogate Development Guidelines

Auto-generated from all feature plans. Last updated: 2025-11-22

## Active Technologies

- C++17 or C++20 (modern C++ with RAII, std::optional, std::variant) (001-tunnel-core-server)

## Project Structure

```text
src/
tests/
```

## Commands

# Add commands for C++17 or C++20 (modern C++ with RAII, std::optional, std::variant)

## Code Style

C++17 or C++20 (modern C++ with RAII, std::optional, std::variant): Follow standard conventions

## Recent Changes

- 001-tunnel-core-server: Added C++17 or C++20 (modern C++ with RAII, std::optional, std::variant)

<!-- MANUAL ADDITIONS START -->

## CRITICAL: macOS Terminal Commands

**NEVER use `timeout` command on macOS** - it does not exist by default.
- ❌ WRONG: `timeout 600 ./script.sh`
- ✅ CORRECT: `./script.sh` (user can Ctrl+C if needed)
- ✅ ALTERNATIVE: Check if `gtimeout` exists first: `command -v gtimeout >/dev/null 2>&1 && gtimeout 600 ./script.sh || ./script.sh`

This has caused multiple deployment failures. Always run commands directly without timeout wrapper on macOS.

## Communication Rules: NO Performative Responses

**DO NOT:**
- Say "You're absolutely right" or similar performative agreement
- Use fake empathy phrases like "I understand your frustration"
- Apologize repeatedly for the same type of mistake
- Provide verbose explanations when a fix is what's needed

**DO:**
- Fix things properly the first time
- If you don't know something, say so directly
- When correcting a mistake, just correct it without theatrical acknowledgment
- Keep responses brief and action-focused

**Why:** Performative empathy wastes time and is insulting. The user wants competent execution, not emotional theater.

<!-- MANUAL ADDITIONS END -->
