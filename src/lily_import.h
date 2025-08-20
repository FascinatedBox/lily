#ifndef LILY_IMPORT_H
# define LILY_IMPORT_H

# include "lily_core_types.h"

struct lily_parse_state_;

/* The import state (ims) holds data relevant to the interpreter's import hook.
   Rewind does not need to adjust any fields because they are set before running
   the import hook. */
typedef struct lily_import_state_ {
    /* Buffer for constructing paths into. */
    lily_msgbuf *path_msgbuf;

    /* This is set to NULL before running the import hook. If an import function
       succeeds, this is non-NULL. */
    lily_module_entry *last_import;

    /* This module called for the import. */
    lily_module_entry *source_module;

    /* Strictly for the import hook (might be NULL/invalid outside of it). This
       is the name that the imported module will have. It is also the name used
       in symbol searches if using `lily_import_library`. */
    const char *pending_loadname;

    /* The full path that was handed to import. This provides a nicer means of
       getting lex->label. Used internally. */
    const char *fullname;

    /* The directory the user passed. Goes between the source's root and the
       target. */
    const char *dirname;

    /* 1 if fullname has slashes, 0 otherwise. Used internally. */
    uint16_t is_slashed_path;

    /* 1 if a package import, 0 otherwise. If 1, path building adds a package
       base directory after the dirname above. */
    uint16_t is_package_import;

    uint32_t pad;
} lily_import_state;

/* The above struct is simple enough that parser handles setup+free. */

void lily_default_import_func(lily_state *, const char *);
const char *lily_ims_build_path(lily_import_state *, const char *,
        const char *);
char *lily_ims_dir_from_path(const char *);
void lily_ims_link_module_to(lily_module_entry *, lily_module_entry *,
        const char *);
lily_module_entry *lily_ims_new_module(struct lily_parse_state_ *);
lily_module_entry *lily_ims_open_module(struct lily_parse_state_ *);

#endif
