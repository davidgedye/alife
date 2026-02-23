CC      = gcc
CFLAGS  = -O3 -march=native -std=c11 -Wall -Wextra -pthread
LDFLAGS = -pthread

TARGET  = bff
SRC     = bf.c

.PHONY: all clean test asan

all: $(TARGET)

$(TARGET): $(SRC) bf.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

asan: $(SRC) bf.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -o $(TARGET)_asan $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET) $(TARGET)_asan

# Quick smoke test
test: $(TARGET)
	@echo 'Testing Hello World...'
	@result=$$(echo '++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.' | ./$(TARGET)); \
	 ascii=$$(echo "$$result" | awk '{for(i=2;i<=NF;i++) printf "%c", strtonum("0x"$$i)}'); \
	 echo "Bytes:  $$result"; \
	 printf "ASCII:  %s\n" "$$ascii"; \
	 [ "$$ascii" = "Hello World!" ] && echo "PASS: Hello World!" || (echo "FAIL: Hello World!"; exit 1)
	@echo 'Testing empty program...'
	@result=$$(printf '' | ./$(TARGET)); \
	 [ -z "$$result" ] && echo "PASS: empty input produces no output" || (echo "FAIL: expected no output, got: $$result"; exit 1)
	@echo 'Testing simple increment...'
	@result=$$(printf '++++.\n' | ./$(TARGET)); \
	 [ "$$result" = "OK 04" ] && echo "PASS: ++++. = 0x04" || (echo "FAIL: expected 'OK 04', got: $$result"; exit 1)
	@echo 'Testing loop skip...'
	@result=$$(printf '[+++].\n' | ./$(TARGET)); \
	 [ "$$result" = "OK 00" ] && echo "PASS: [+++]. = 0x00 (loop skipped)" || (echo "FAIL: expected 'OK 00', got: $$result"; exit 1)
	@echo 'Testing unmatched bracket...'
	@result=$$(printf '[unmatched\n' | ./$(TARGET)); \
	 [ "$$result" = "ERR" ] && echo "PASS: unmatched bracket = ERR" || (echo "FAIL: expected 'ERR', got: $$result"; exit 1)
	@echo 'All tests passed.'
