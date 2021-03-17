SRCS=roidserver.c
HEADERS=
COMMON_DEPS=Makefile
STATIC_ANALYZE=-fsanitize=address -fsanitize=undefined
DEBUG_CFLAGS=-g $(STATIC_ANALYZE)
WARNINGS=-Wno-error=format -Wno-format -Wall -Werror -Wall -Wpedantic -Wno-unknown-attributes -Wno-ignored-optimization-argument -Wno-unknown-pragmas  -Wmissing-field-initializers -Wfatal-errors -Wextra -Wshadow -Wuninitialized  -Wundef -Wbad-function-cast -Wparentheses -Wnull-dereference -pedantic-errors

OBJS=$(addprefix build/obj/, $(SRCS:.c=.o))
CFLAGS=$(WARNINGS) $(DEBUG_CFLAGS)

all: build/roid.d


build/obj/%.o: %.c $(HEADERS) $(COMMON_DEPS)
	@mkdir -p build/obj
	$(CC) -c $(CFLAGS) $*.c -o build/obj/$*.o

build/roid.d: $(OBJS)
	$(CC) $(LDFLAGS) $(CFLAGS) $(OBJS) -o build/roid.d $(LIBS)

clean:
	rm -rf build
