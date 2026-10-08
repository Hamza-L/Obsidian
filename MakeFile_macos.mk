# Cocoa/Metal frameworks and Objective-C come with Xcode Command Line Tools.
$(call require_probe,Xcode Command Line Tools,xcode-select -p,/,Install with: xcode-select --install)

SRC_M += src/platform/macos/p_window.m
# SRC_M += src/platform/macos/p_renderer.m

SRC_C += src/memory/m_alloc_macos.c

# Link against the system frameworks the Cocoa + Metal backends need.
# Toolchain (LD=clang) and empty LDFLAGS are inherited from the top-level Makefile.
LDFLAGS += -framework Cocoa
LDFLAGS += -framework Metal -framework QuartzCore
