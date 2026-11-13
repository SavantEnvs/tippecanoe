// Force-included in the sanitized build. protozero (vendored, header-only) guards its wire-type
// accessors with assert(); upstream's release build compiles those out via -DNDEBUG, so a tile
// whose field has the wrong wire type is merely mis-parsed there. Without this every such tile
// aborts the fuzzer on a debug-only check. config.hpp provides this macro as its override point;
// tippecanoe's own assert() calls stay enabled.
#ifndef protozero_assert
#define protozero_assert(x) ((void) 0)
#endif
