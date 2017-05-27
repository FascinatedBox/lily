#include "lily_library.h"
#include "lily_alloc.h"

#ifdef _WIN32
#include <Windows.h>

lily_library *lily_library_load(const char *path)
{
    HMODULE handle = LoadLibraryA(path);
    if (handle == NULL)
        return NULL;

    lily_library *lib = lily_malloc(sizeof(*lib));
    lib->source = handle;
    return lib;
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

lily_library *lily_library_load(const char *path)
{
    void *handle = dlopen(path, RTLD_LAZY);
    if (handle == NULL)
        return NULL;

    lily_library *lib = lily_malloc(sizeof(*lib));
    lib->source = handle;

    return lib;
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
