# Formatting

Style is defined in `.clang-format` (4-space indent, 120 columns,
Allman braces for functions, `Type *p` pointers, include order untouched).

## Install clang-format

- Windows: `winget install LLVM.LLVM` (provides `clang-format.exe`)
- Ubuntu: `sudo apt-get install clang-format`
- macOS: `brew install clang-format`

## Usage

Format only the files you touched:

```sh
clang-format -i <file1.c> <file2.h>
```

Do NOT run whole-tree rewrites (`clang-format -i` over the entire repo);
keep diffs limited to your own changes.

CI may check formatting, so run `clang-format -i` on touched files
before pushing.
