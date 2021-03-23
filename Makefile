SRCS=roidserver.c
HEADERS=
COMMON_DEPS=Makefile

STATIC_ANALYZE=-fsanitize=address -fsanitize=undefined
DEBUG_CFLAGS=-g $(STATIC_ANALYZE)
WARNINGS=-Wno-error=format -Wno-format -Wall -Werror -Wall -Wpedantic -Wno-unknown-attributes -Wno-ignored-optimization-argument -Wno-unknown-pragmas  -Wmissing-field-initializers -Wfatal-errors -Wextra -Wshadow -Wuninitialized  -Wundef -Wbad-function-cast -Wparentheses -Wnull-dereference -pedantic-errors

OBJS=$(addprefix build/obj/, $(SRCS:.c=.o))
CFLAGS=-O2 $(WARNINGS) $(DEBUG_CFLAGS)


WIN32_OBJS=$(addprefix build/win32/, $(SRCS:.c=.o))
WIN32_CC=/usr/local/mingw/bin/x86_64-w64-mingw32-gcc
WIN32_CFLAGS=$(WARNINGS)
WIN32_LIBS=-lws2_32

AMIGA_OBJS=$(addprefix build/amiga/, $(SRCS:.c=.o))
AMIGA_CC=/usr/local/amiga/bebbo/bin/m68k-amigaos-gcc
AMIGA_CFLAGS=-O2 -DAMIGA -noixemul -fomit-frame-pointer
AMIGA_LDFLAGS=-s
AMIGA_LIBS=-lamiga

build/roid.d:

amiga: build/amiga/roid.d
win32: build/win32/roid.exe
all: build/roid.d amiga win32

build/obj/%.o: %.c $(HEADERS) $(COMMON_DEPS)
	@mkdir -p build/obj
	$(CC) -c $(CFLAGS) $*.c -o build/obj/$*.o

build/amiga/%.o: %.c $(HEADERS) $(COMMON_DEPS)
	@mkdir -p build/amiga
	$(AMIGA_CC) -c $(AMIGA_CFLAGS) $*.c -o build/amiga/$*.o

build/win32/%.o: %.c $(HEADERS) $(COMMON_DEPS)
	@mkdir -p build/win32
	$(WIN32_CC) -c $(WIN32_CFLAGS) $*.c -o build/win32/$*.o

build/roid.d: $(OBJS)
	$(CC) $(LDFLAGS) $(CFLAGS) $(OBJS) -o build/roid.d $(LIBS)

build/amiga/roid.d: $(AMIGA_OBJS)
	$(AMIGA_CC) $(AMIGA_LDFLAGS) $(AMIGA_CFLAGS) $(AMIGA_OBJS) -o build/amiga/roid.d $(AMIGA_LIBS)

build/win32/roid.exe: $(WIN32_OBJS)
	@mkdir -p build/win32
	$(WIN32_CC) $(WIN32_LDFLAGS) $(WIN32_CFLAGS) $(WIN32_OBJS) -o build/win32/roid.exe $(WIN32_LIBS)

clean:
	rm -rf build
