/* This file defines functions that a lily implementation must define. */

/* Tells the server running lily to send a chunk of HTML data. The data is not
   to be free'd. */
void lily_impl_send_html(char *);

/* This is called when lily has encountered a fatal error. */
void lily_impl_fatal_error(char *, ...);
