#ifndef LILY_IMPORT_H
# define LILY_IMPORT_H

# include "lily_core_types.h"

struct lily_parse_state_;

/* The type of import being performed. There is no 'none' value because path
   building checks for `dirname == NULL` instead. */
typedef enum {
    imp_local,
    imp_package,
    imp_system
} lily_import_type;

/* The import state (ims) is where all modules are created and held. It also
   holds data relevant to the interpreter's import hook, which handles module
   discovery. This state does not have a rewind, because relevant fields are set
   before importing begins. */
typedef struct lily_import_state_ {
    /* The prelude is also the start when walking modules. */
    lily_module_entry *prelude;
    lily_module_entry *module_top;

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

    /* System directories are stored here with \0 between each one. The last
       entry is denoted by LILY_PATH_CHAR. */
    char *sys_dirs;

    /* 1 if fullname has slashes, 0 otherwise. Used internally. */
    uint16_t is_slashed_path;

    /* 1 if a package import, 0 otherwise. If 1, path building adds a package
       base directory after the dirname above. */
    lily_import_type import_type: 16;
    uint16_t next_module_id;
    uint16_t pad;
} lily_import_state;

lily_import_state *lily_new_import_state(void);
void lily_free_import_state(lily_import_state *);

void lily_default_import_func(lily_state *, const char *);
const char *lily_ims_build_path(lily_import_state *, const char *,
        const char *);
lily_module_entry *lily_ims_create_main(lily_import_state *);
char *lily_ims_dir_from_path(const char *);
void lily_ims_link_module_to(lily_module_entry *, lily_module_entry *,
        const char *);
lily_module_entry *lily_ims_open_module(struct lily_parse_state_ *);
void lily_ims_process_sys_dirs(struct lily_parse_state_ *, lily_config *);

#endif
