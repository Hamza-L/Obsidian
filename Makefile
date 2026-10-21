# Toolchain defaults are platform-neutral; platform makefiles (included below)
# override CC/LDFLAGS as needed.
CC := clang
ASAN := 0

# Public target name (what users type): no extension, consistent across platforms.
APP_NAME := Obsidian

# Platform detection (works from cmd.exe, PowerShell, and MSYS2/Git Bash).
# On native Windows, $(OS) is always Windows_NT — skip uname entirely.
IS_WINDOWS :=
ifeq ($(OS),Windows_NT)
IS_WINDOWS := 1
else
UNAME_S := $(shell uname -s)
ifneq ($(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)),)
IS_WINDOWS := 1
endif
endif

ifeq ($(IS_WINDOWS),1)
EXE_EXT := .exe
else
EXE_EXT :=
endif

BUILD_DIR := build
OUTPUT := $(BUILD_DIR)/$(APP_NAME)$(EXE_EXT)
TEST_OUTPUT := $(BUILD_DIR)/$(APP_NAME)_test$(EXE_EXT)

# Dependency makefiles may be included before targets are defined; make sure the
# default goal stays consistent.
.DEFAULT_GOAL := all

# `make V=1` echoes the underlying compile/link commands.
ifeq ($(V),1)
Q :=
else
Q := @
endif

# Graphics API validation, on for debug builds. Override with `make RENDERER_VALIDATION=0`.
RENDERER_VALIDATION ?= 1
CFLAGS := -std=c99 -g -DDEBUG -Wall -Wextra -Wpedantic -Werror -DRENDERER_VALIDATION=$(RENDERER_VALIDATION)
CFLAGS += -I./external
CFLAGS += -I./include
CFLAGS += -I./src
CFLAGS += -I./src/input

# Platform makefiles set this to change the language the unity roots compile as.
LANGFLAGS :=
LDFLAGS :=
LDLIBS :=

ifeq ($(ASAN),1)
CFLAGS  += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif

# Unity build: each executable is one translation unit compiled and linked in a single
# step. main.c and test_main.c include src/unity.c, which includes every module and the
# platform layer. Generated .d files track the included sources.
APP_ROOT := src/main.c
TEST_ROOT := test/test_main.c

# Platform makefiles can append to this to force prerequisites before any
# object is compiled (e.g. downloaded headers).
EXTRA_OBJECT_DEPS :=

# Default platform settings (may be overridden by platform makefiles).
mkdir_p = mkdir -p "$(1)"
rmdir_rf = rm -rf "$(1)"
run = "$(1)"

# --- Dependencies: stb_image (single-header) ------------------------------
STB_IMAGE_COMMIT ?= 013ac3beddff3dbffafd5177e7972067cd2b5083
STB_DIR := external/stb
STB_IMAGE_HEADER := $(STB_DIR)/stb_image.h
STB_IMAGE_URL := https://raw.githubusercontent.com/nothings/stb/$(STB_IMAGE_COMMIT)/stb_image.h

EXTRA_OBJECT_DEPS += $(STB_IMAGE_HEADER)

$(STB_IMAGE_HEADER):
	@echo "DEPS stb_image ($(STB_IMAGE_COMMIT))"
	@$(call mkdir_p,$(STB_DIR))
	@curl -sSfL -o "$(STB_IMAGE_HEADER)" "$(STB_IMAGE_URL)"

# Preflight checks — parse-time, fail fast with an install hint before any
# recipe runs. NOTE: commas in the hint must be written as $(comma) —
# $(call) splits on ",".
comma := ,
# $(1) = binary, $(2) = expected substring in `--version`, $(3) = install hint.
define require_tool
$(if $(findstring $(2),$(shell $(1) --version 2>&1)),,$(error Missing required tool '$(1)'. $(3)))
endef
# $(1) = label for the error, $(2) = probe command, $(3) = expected substring
# in its output, $(4) = install hint. Use when --version doesn't fit (custom
# subcommand, tools without --version, or output filtering).
define require_probe
$(if $(findstring $(3),$(shell $(2) 2>&1)),,$(error Missing $(1). $(4)))
endef

# Cross-platform tools needed before we even reach the platform include.
$(call require_tool,clang,clang version,Install LLVM/Clang from https://releases.llvm.org and put it on PATH.)
$(call require_tool,curl,curl,curl ships with Windows 10+ and macOS. Install Git for Windows or your distro's curl package.)

# Web (Emscripten/WebGPU) is an explicit opt-in: `make PLATFORM=web`.
ifeq ($(PLATFORM),web)
include MakeFile_emscripten.mk
else ifeq ($(IS_WINDOWS),1)
include MakeFile_win32.mk
else ifeq ($(UNAME_S),Darwin)
include MakeFile_macos.mk
else
$(error Unsupported platform: $(UNAME_S))
endif

DEPFLAGS := -MMD -MP
-include $(OUTPUT).d $(TEST_OUTPUT).d

# $(1) = root source, $(2) = output executable.
build_exe = $(CC) $(CFLAGS) $(LANGFLAGS) $(DEPFLAGS) -MF $(2).d $(1) -o $(2) $(LDFLAGS) $(LDLIBS)

.PHONY: all clean clean-full test deps $(APP_NAME)

# `make deps` fetches/builds every registered dependency without touching the
# app itself — useful for populating external/ from a clean checkout.
deps: $(EXTRA_OBJECT_DEPS)

# Default build (same command on every platform):
#   make
# builds the actual output file (with .exe on Windows).
all: $(APP_NAME)

# `make test` builds and runs the module test suite.
test: $(TEST_OUTPUT)
	@$(call run,$(TEST_OUTPUT))

$(TEST_OUTPUT): $(TEST_ROOT) $(EXTRA_OBJECT_DEPS) | $(BUILD_DIR)
	@echo BUILD $@
	$(Q)$(call build_exe,$<,$@)

# User-facing target without extension.
$(APP_NAME): $(OUTPUT)

$(OUTPUT): $(APP_ROOT) $(EXTRA_OBJECT_DEPS) | $(BUILD_DIR)
	@echo BUILD $@
	$(Q)$(call build_exe,$<,$@)

$(BUILD_DIR):
	$(Q)$(call mkdir_p,$(BUILD_DIR))

clean:
	$(Q)$(call rmdir_rf,build)

clean-full: clean
	$(Q)$(call rmdir_rf,external)
