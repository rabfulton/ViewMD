CC = gcc
CFLAGS = -Wall -Wextra -O2 -g `pkg-config --cflags gtk+-3.0`
LDFLAGS = `pkg-config --libs gtk+-3.0`

SRCDIR = src
OBJDIR = obj
BINDIR = .

SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o) $(OBJDIR)/md4c.o
TARGET = $(BINDIR)/viewmd

PREFIX ?= /usr/local
DESTDIR ?=

bindir ?= $(PREFIX)/bin
datadir ?= $(PREFIX)/share
applicationsdir ?= $(datadir)/applications

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/md4c.o: $(SRCDIR)/md4c/md4c.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(bindir)/viewmd
	install -Dm644 assets/viewmd.desktop $(DESTDIR)$(applicationsdir)/viewmd.desktop

uninstall:
	rm -f $(DESTDIR)$(bindir)/viewmd
	rm -f $(DESTDIR)$(applicationsdir)/viewmd.desktop

# Header dependencies
$(OBJDIR)/main.o: $(SRCDIR)/app.h $(SRCDIR)/window.h
$(OBJDIR)/app.o: $(SRCDIR)/app.h $(SRCDIR)/config.h $(SRCDIR)/window.h $(SRCDIR)/editor.h
$(OBJDIR)/window.o: $(SRCDIR)/window.h $(SRCDIR)/app.h $(SRCDIR)/editor.h $(SRCDIR)/config.h
$(OBJDIR)/editor.o: $(SRCDIR)/editor.h $(SRCDIR)/markdown.h $(SRCDIR)/app.h
$(OBJDIR)/markdown.o: $(SRCDIR)/markdown.h $(SRCDIR)/code_highlight.h
$(OBJDIR)/code_highlight.o: $(SRCDIR)/code_highlight.h
$(OBJDIR)/config.o: $(SRCDIR)/config.h
$(OBJDIR)/md4c.o: $(SRCDIR)/md4c/md4c.h
