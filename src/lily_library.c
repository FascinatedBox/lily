#include "lily_alloc.h"
#include "lily_library.h"

#ifdef _WIN32
#include <Windows.h>

void *lily_library_load(const char *path)
{
    return LoadLibraryA(path);
}

void *lily_library_get(void *source, const char *target)
{
    return GetProcAddress((HMODULE)source, target);
}

void lily_library_free(void *source)
{
    FreeLibrary((HMODULE)source);
}
#else
#include <dlfcn.h>

void *lily_library_load(const char *path)
{
    return dlopen(path, RTLD_LAZY);
}

void *lily_library_get(void *source, const char *name)
{
    return dlsym(source, name);
}

void lily_library_free(void *source)
{
    dlclose(source);
}
#endif
