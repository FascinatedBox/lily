#include "lily_library.h"

#include "lily_api_alloc.h"

#ifdef _WIN32
#include <Windows.h>

lily_library *lily_library_load(const char *path)
{
    HMODULE handle = LoadLibraryA(path);
    if (handle == NULL)
        return NULL;

    const void *dynaload_table = GetProcAddress(handle, "lily_dynaload_table");
    if (dynaload_table == NULL) {
        FreeLibrary(handle);
        return NULL;
    }

    lily_library *lib = lily_malloc(sizeof(*lib));
    lib->source = handle;
    lib->dynaload_table = dynaload_table;
    return lib;
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

    const void *dynaload_table = dlsym(handle, "lily_dynaload_table");
    if (dynaload_table == NULL) {
        dlclose(handle);
        return NULL;
    }

    lily_library *lib = lily_malloc(sizeof(lily_library));
    lib->source = handle;
    lib->dynaload_table = dynaload_table;

    return lib;
}

void lily_library_free(void *source)
{
    dlclose(source);
}
#endif
