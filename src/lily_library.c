#include <dlfcn.h>

#include "lily_alloc.h"
#include "lily_library.h"

lily_library *lily_library_load(char *path)
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

void lily_library_free(lily_library *lib)
{
    dlclose(lib->source);
    lily_free(lib);
}
