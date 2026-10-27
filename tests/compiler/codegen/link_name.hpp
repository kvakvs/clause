#pragma once

// Bind a declaration to a generated symbol; Mach-O prefixes C-level symbol names with '_'. Added for macOS consumers.
#ifdef __APPLE__
#define CLAUSE_LINK_NAME(name) asm("_" name)
#else
#define CLAUSE_LINK_NAME(name) asm(name)
#endif
