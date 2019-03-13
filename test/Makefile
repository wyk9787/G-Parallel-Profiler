CC      := clang
CCFLAGS := -std=c11 -g -Wall
LDFLAGS := -lpthread -lcrypto -lm

all: test password-cracker

test: test.c
	$(CC) $(CCFLAGS) $(LDFLAGS) -o $@ $^	

password-cracker: password-cracker.c
	$(CC) $(CCFLAGS) $(LDFLAGS) -o $@ $^	

clean:
	rm test
	rm password-cracker
