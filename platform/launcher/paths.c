/* File and folder questions the launcher asks (launcher.h). */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "launcher.h"
#ifdef __APPLE__
#include <limits.h>
#include <stdlib.h>
#include <mach-o/dyld.h>
#endif

const char *Launcher_FileName(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/' || *p == '\\') name = p + 1;
    return name;
}

/* Windows paths ignore case and take either separator. */
int Launcher_SamePath(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x == '\\') x = '/';
        if (y == '\\') y = '/';
#ifdef _WIN32
        x = SDL_tolower(x); y = SDL_tolower(y);
#endif
        if (x != y) return 0;
        if (!x) return 1;
    }
}

int Launcher_FileExists(const char *path)
{
    SDL_PathInfo info;
    return path && path[0] && SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

int Launcher_IsFolder(const char *path)
{
    SDL_PathInfo info;
    return path && path[0] && SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

int Launcher_HasExtension(const char *path, const char *extension)
{
    const char *name = Launcher_FileName(path), *dot = strrchr(name, '.');
    return dot && !SDL_strcasecmp(dot + 1, extension);
}

char *Launcher_ProgramFolder(void)
{
#ifdef __APPLE__
    /* SDL's base path is the bundle's Resources; the programs are in Contents/MacOS. */
    char path[PATH_MAX], real[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) || !realpath(path, real)) return NULL;
    char *slash = strrchr(real, '/');
    if (!slash) return NULL;
    slash[1] = 0;
    return SDL_strdup(real);
#else
    const char *base = SDL_GetBasePath();
    return SDL_strdup(base ? base : "./");
#endif
}

void Launcher_CleanPath(char *path)
{
#ifdef _WIN32
    for (char *p = path; *p; ++p)
        if (*p == '/') *p = '\\';
#else
    (void)path;
#endif
}
