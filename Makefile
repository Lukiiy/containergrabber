CC := gcc
CFLAGS := -O2 -std=c11 -Wall -Wextra -Wpedantic
LDLIBS ?= -lz -llz4

TARGET = containergrabber
SRCS := main.c world.c nbtReader.c
OBJS := $(SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) $(DEPS) *.o *.d

.PHONY: all clean

-include $(DEPS)