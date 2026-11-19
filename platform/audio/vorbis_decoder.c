/* stb_vorbis (public domain or MIT, Sean Barrett) built once, for
   recorded_music.c: Ogg Vorbis songs decoded from memory. */
#define STB_VORBIS_NO_STDIO
/* stb looks for alloca only on Linux and Windows; with _POSIX_C_SOURCE set,
   macOS's stdlib.h does not declare it either. */
#if defined(__APPLE__)
#include <alloca.h>
#endif
#include "stb_vorbis.c"
