#ifndef LILY_PKG_BUILTIN_H
# define LILY_PKG_BUILTIN_H

struct lily_symtab_;
struct lily_import_entry_;

void lily_init_builtin_package(struct lily_symtab_ *, struct lily_import_entry_ *);

#endif
