CC     := clang
CFLAGS := -g -Wall 
LFLAGS := -lpthread -lm -lcrypto
SRCS   := $(wildcard *.c)

all: $(SRCS:.c=)

clean:
	    rm -rf $(SRCS:.c=)

%: %.c
	    $(CC) $(CFLAGS) $(LFLAGS) -o $@ $^

