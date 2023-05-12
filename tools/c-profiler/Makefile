# Define directories
SRCDIR = ../../tamp/_c_src
BUILDDIR = build
TARGET = build/executable

# Collect all .c and .h files
SRCS = $(shell find $(SRCDIR) -name "*.c")
MAIN_SRC = main.c
HEADERS = $(shell find $(SRCDIR) -name "*.h")

# Define the object files
OBJS =
OBJS += $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))
OBJS += $(patsubst %.c,$(BUILDDIR)/%.o,$(MAIN_SRC))

# Compiler and linker flags
CFLAGS = -g -O3 -Wall -I$(SRCDIR)
LDFLAGS = # Linker flags, e.g. -lm for math library

# Default rule
all: directories $(TARGET)

# Rule to create the target
$(TARGET): $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

# Rule to create object files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: %.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: directories
directories:
	@mkdir -p $(BUILDDIR)
	@mkdir -p $(dir $(TARGET))

.PHONY: clean
clean:
	@rm -rf $(BUILDDIR) $(TARGET)

profile: all
	xctrace record --template 'Time Profiler' --launch ./build/executable

profile-simple: all
	time ./build/executable
