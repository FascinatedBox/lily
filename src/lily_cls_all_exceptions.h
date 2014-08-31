#ifndef LILY_CLS_ALL_EXCEPTIONS_H
# define LILY_CLS_ALL_EXCEPTIONS_H

extern int lily_nomemoryerror_setup(lily_class *);
extern int lily_dbzerror_setup(lily_class *);
extern int lily_indexerror_setup(lily_class *);
extern int lily_badtcerror_setup(lily_class *);
extern int lily_noreturnerror_setup(lily_class *);
extern int lily_valueerror_setup(lily_class *);
extern int lily_recursionerror_setup(lily_class *);
extern int lily_keyerror_setup(lily_class *);
extern int lily_formaterror_setup(lily_class *);

#endif
