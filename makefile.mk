# FocusForge Makefile
# Build system for FocusForge Pomodoro timer and task manager

# Compiler and flags
CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2
LDFLAGS = -lncurses

# Directories
SRCDIR = src
OBJDIR = obj
BINDIR = bin

# Target executable
TARGET = $(BINDIR)/focusforge
TEST_TARGET = $(BINDIR)/focusforge_test

# Source files
SOURCES = $(SRCDIR)/focusforge.c
TEST_SOURCES = $(SRCDIR)/focusforge_test.c

# Object files
OBJECTS = $(SOURCES:$(SRCDIR):$(OBJDIR))
TEST_OBJECTS = $(TEST_SOURCES:$(SRCDIR):$(OBJDIR))

# Default target
.PHONY: all clean test install uninstall help

all: $(TARGET)

# Build main application
 $(TARGET): $(OBJECTS)
    @echo "Building FocusForge..."
    $(CC) $(CFLAGS) $(OBJECTS) $(LDFLAGS) -o $@

# Build test suite
 $(TEST_TARGET): $(TEST_OBJECTS)
    @echo "Building FocusForge tests..."
    $(CC) $(CFLAGS) $(TEST_OBJECTS) -o $@

# Create object directories
 $(OBJDIR):
    mkdir -p $(OBJDIR)

# Compile source files
 $(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
    @echo "Compiling $<"
    $(CC) $(CFLAGS) -c $< -o $@

# Install application
install: $(TARGET)
    @echo "Installing FocusForge to /usr/local/bin..."
    install -d /usr/local/bin $(TARGET)

# Uninstall application
uninstall:
    @echo "Removing FocusForge from /usr/local/bin..."
    rm -f /usr/local/bin/focusforge

# Run tests
test: $(TEST_TARGET)
    @echo "Running FocusForge test suite..."
    ./$(TEST_TARGET)

# Clean build artifacts
clean:
    @echo "Cleaning build artifacts..."
    rm -rf $(OBJDIR) $(BINDIR)

# Create necessary directories
setup: $(OBJDIR)
    mkdir -p $(HELP_DIR)

# Help information
help:
    @echo "FocusForge Makefile"
    @echo ""
    @echo "Available targets:"
    @echo "  all      - Build FocusForge"
    @echo "  test     - Build and run tests"
    @echo "  clean    - Clean build artifacts"
    @echo "  install  - Install to /usr/local/bin"
    @echo "  uninstall- Remove from /usr/local/bin"
    @echo "  help     - Show this help message"
    @echo ""
    @echo "Examples:"
    @echo "  make all      # Build FocusForge"
    echo "  make test     # Run test suite"
    echo "  make install  # Install system-wide"
    @echo "  make clean    # Clean build files"

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: $(TARGET)

# Release build with optimization
release: CFLAGS += -DNDEBUG -s

# Static analysis with cppcheck (if available)
static-analysis:
    @if command -v cppcheck > /dev/null 2>&1; then \
        echo "Running static analysis..."; \
        cppcheck --enable=all --std=c99 $(SOURCES); \
    fi

# Check for memory leaks with valgrind (if available)
memcheck: $(TARGET)
    @if command -v valgrind > /dev/null 2>&1; then \
        echo "Running memory leak check..."; \
        valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET); \
    fi

# Generate documentation (if doxygen is available)
docs:
    @if command -v doxygen > /dev/null 2>&1; then \
        echo "Generating documentation..."; \
        doxygen Doxyfile; \
    fi

# Format code with clang-format (if available)
format:
    @if command -v clang-format > /dev/null 2>&1; then \
        echo "Formatting code..."; \
        clang-format -i $(SOURCES); \
    fi

# Cross-platform compatibility check
check:
    @echo "Checking build compatibility..."
    @echo "CC: $(CC)"
    @echo "CFLAGS: $(CFLAGS)"
    @echo "LDFLAGS: $(LDFLAGS)"
    @echo "Target: $(TARGET)"
    @echo "Test target: $(TEST_TARGET)"

# Show build configuration
config:
    @echo "Build Configuration:"
    @echo "  CC: $(CC)"
    @echo "  CFLAGS: $(CFLAGS)"
    @echo "  LDFLAGS: $(LDFLAGS)"
    @echo "  SRCDIR: $(SRCDIR)"
    @echo "  OBJDIR: $(OBJDIR)"
    @```

# Create distribution package
dist: clean
    @echo "Creating distribution package..."
    @mkdir -p dist/focusforge-$(VERSION)
    cp focusforge.c dist/focusforge-$(VERSION)/
    cp README.md dist/focusforge-$(VERSION)/
    cp Makefile dist/focusforge-$(VERSION)/
    cp LICENSE dist/focusforge-$(VERSION)/
    tar -czf dist/focusforge-$(VERSION).tar.gz dist/focusforge-~$(VERSION)

# Install from source
install-from-source: $(TARGET)
    @echo "Installing FocusForge from source..."
    install -m 0755 $(TARGET) /usr/local/bin

# Uninstall from source
uninstall-from-source:
    @echo "Uninstalling FocusForge..."
    rm -f /usr/local/bin/focusforge

# Continuous integration
ci: clean all test
    @echo "Continuous integration complete"
