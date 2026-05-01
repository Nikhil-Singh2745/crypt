# crypt.c — single-binary roguelike HTTP server
# we use no libraries because we hate ourselves and our users

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -pthread -std=c11
LDFLAGS ?= -pthread

SRCS    := src/main.c src/server.c src/router.c src/session.c \
           src/game.c src/map.c src/fov.c src/render.c
OBJS    := $(SRCS:src/%.c=build/%.o)
BIN     := build/crypt
EMBED   := src/static_html.h

.PHONY: all clean run docker

all: $(BIN)

$(BIN): $(OBJS) | build
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

build/%.o: src/%.c $(EMBED) | build
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

# embed the frontend into the binary as a byte array
$(EMBED): static/index.html
	@cd static && xxd -i index.html > ../$(EMBED).tmp
	@mv $(EMBED).tmp $(EMBED)

build:
	@mkdir -p build

run: $(BIN)
	./$(BIN)

clean:
	rm -rf build $(EMBED)

docker:
	docker build -t crypt .
