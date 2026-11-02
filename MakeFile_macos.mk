# Cocoa/Metal frameworks and Objective-C come with Xcode Command Line Tools.
$(call require_probe,Xcode Command Line Tools,xcode-select -p,/,Install with: xcode-select --install)
$(call require_probe,Metal toolchain,xcrun -sdk macosx metal --version,Apple metal version,Install with: xcodebuild -downloadComponent MetalToolchain)

# The platform layer is Objective-C, so the unity roots compile as Objective-C here.
LANGFLAGS := -x objective-c -fobjc-arc -fmodules

# Link against the system frameworks the Cocoa + Metal backends need.
# Toolchain (LD=clang) and empty LDFLAGS are inherited from the top-level Makefile.
LDFLAGS += -framework Cocoa
LDFLAGS += -framework Metal -framework QuartzCore

# Shaders compile offline, one .metallib per source: src/shaders/foo.metal -> build/shaders/foo.metallib
SHADER_DIR := src/shaders
SHADER_BUILD_DIR := $(BUILD_DIR)/shaders
SHADER_SOURCES := $(wildcard $(SHADER_DIR)/*.metal)
SHADER_LIBS := $(patsubst $(SHADER_DIR)/%.metal,$(SHADER_BUILD_DIR)/%.metallib,$(SHADER_SOURCES))

METALFLAGS := -std=metal4.0 -Wall -Werror -I./$(SHADER_DIR) -I./src/platform/macos
ifeq ($(RENDERER_VALIDATION),1)
METALFLAGS += -frecord-sources -gline-tables-only
endif

$(APP_NAME): $(SHADER_LIBS)

$(SHADER_BUILD_DIR)/%.metallib: $(SHADER_DIR)/%.metal | $(SHADER_BUILD_DIR)
	@echo METAL $<
	$(Q)xcrun -sdk macosx metal $(METALFLAGS) -MMD -MP -MF $@.d -MT $@ $< -o $@

$(SHADER_BUILD_DIR):
	$(Q)$(call mkdir_p,$@)

-include $(SHADER_LIBS:=.d)
