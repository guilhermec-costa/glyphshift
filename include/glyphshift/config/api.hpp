#pragma once

// Interface of the shared editor library (configuration, presets, characters
// and the editor model). Linux builds it as a shared object with hidden
// symbols, so these declarations are exported; Windows links it statically.
#if defined(__GNUC__)
#define GS_EDITOR_API __attribute__((visibility("default")))
#else
#define GS_EDITOR_API
#endif
