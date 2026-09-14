
# Toolchain
CC := clang-20
LD := clang-20

# Flags
CFLAGS := -Isource -Ithirdparty/source -std=c2x -g
LFLAGS := -lSDL3 -lGL -lopenal -lm

# Find all source files recursively
SRCS := $(shell find . -name '*.c')

# Map source files to object files in obj/
OBJS := $(patsubst ./%, obj/%.o,$(SRCS))

# Dependency files (.d)
DEPS := $(OBJS:.o=.d)

# Default target
all: smb

# Rule for binary
smb: $(OBJS) makefile
	@mkdir -p $(dir $@)
	$(LD) -o $@ $(LFLAGS) $(OBJS)

# Rule for object files
obj/%.c.o: %.c makefile
	@mkdir -p $(dir $@)
	$(CC) -o $@ -c -MMD -MP $(CFLAGS) $<

# Include dependency files if they exist
-include $(DEPS)

# Clean
clean:
	rm -rf obj
	rm smb

.PHONY: all clean
