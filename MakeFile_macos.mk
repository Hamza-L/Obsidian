# Cocoa/Metal frameworks and Objective-C come with Xcode Command Line Tools.
$(call require_probe,Xcode Command Line Tools,xcode-select -p,/,Install with: xcode-select --install)

# The platform layer is Objective-C, so the unity roots compile as Objective-C here.
LANGFLAGS := -x objective-c -fobjc-arc

# Link against the system frameworks the Cocoa + Metal backends need.
# Toolchain (LD=clang) and empty LDFLAGS are inherited from the top-level Makefile.
LDFLAGS += -framework Cocoa
LDFLAGS += -framework Metal -framework QuartzCore
