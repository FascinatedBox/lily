CC=gcc
CFLAGS=-c -g3

BINDIR=bin
OBJDIR=objdir

CORE_OBJECTS=$(OBJDIR)/lily_parser.o \
			$(OBJDIR)/lily_lexer.o \
			$(OBJDIR)/lily_ast.o \
			$(OBJDIR)/lily_emitter.o \

FS_OBJECTS=$(CORE_OBJECTS) \
			$(OBJDIR)/fs_main.o

all: lily_fs

clean:
	rm -f $(OBJDIR)/* lily_fs

lily_fs: $(FS_OBJECTS)
	$(CC) $(FS_OBJECTS) -o lily_fs

# Becomes an executable
$(OBJDIR)/fs_main.o: fs_main.c lily_lexer.h lily_parser.h
	$(CC) $(CFLAGS) fs_main.c -o $(OBJDIR)/fs_main.o

# Core
$(OBJDIR)/lily_lexer.o: lily_lexer.c lily_lexer.h lily_impl.h
	$(CC) $(CFLAGS) lily_lexer.c -o $(OBJDIR)/lily_lexer.o

$(OBJDIR)/lily_parser.o: lily_parser.c lily_lexer.h lily_impl.h lily_types.h \
						 lily_ast.h
	$(CC) $(CFLAGS) lily_parser.c -o $(OBJDIR)/lily_parser.o

$(OBJDIR)/lily_ast.o: lily_ast.c lily_ast.h lily_types.h lily_impl.h
	$(CC) $(CFLAGS) lily_ast.c -o $(OBJDIR)/lily_ast.o

$(OBJDIR)/lily_emitter.o: lily_emitter.c lily_opcode.h lily_impl.h
	$(CC) $(CFLAGS) lily_emitter.c -o $(OBJDIR)/lily_emitter.o

.PHONY: clean all $(BINDIR)/lily_fs
