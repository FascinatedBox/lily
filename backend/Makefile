CC=gcc
CFLAGS=-c -g3
OBJDIR=objdir

OBJECTS=$(OBJDIR)/lily_page_scanner.o
FS_OBJECTS=$(OBJECTS) \
	$(OBJDIR)/fs_main.o

all: lily_fs

clean:
	rm -f $(OBJDIR)/* lily_fs

lily_fs: $(FS_OBJECTS)
	$(CC) $(FS_OBJECTS) -o lily_fs

$(OBJDIR)/fs_main.o: fs_main.c
	$(CC) $(CFLAGS) fs_main.c -o $(OBJDIR)/fs_main.o

$(OBJDIR)/lily_page_scanner.o: lily_page_scanner.c
	$(CC) $(CFLAGS) lily_page_scanner.c -o $(OBJDIR)/lily_page_scanner.o

.PHONY: clean lily-fs
