APP=shc-decode
CFLAGS=-Wall -pedantic
LDFLAGS=-lz -lb64

all:release

release: CFLAGS=-O3 -g3
release: $(APP)

debug: CFLAGS=-O0 -g3
debug: $(APP)

$(APP): main.c
	$(CC) $^ $(CFLAGS) -o $@ $(LDFLAGS)
