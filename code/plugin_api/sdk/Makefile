# Builds odf-build, the program that builds plugins. It has everything in itself: the preprocessor, the compiler and the
# assembler (../toolchain: lcc and q3asm of ioquake3) and the files of the SDK (headers, C library).
#
#   make                    builds build/odf-build/odf-build (.exe on Windows) in the build folder of the project
#   make OUT=some/folder    builds some/folder/odf-build
#   make CC=clang           another compiler;  CFLAGS=...  LDFLAGS=-static  as you need them
#
# Needs a C compiler that knows -include (gcc, clang, MinGW) and make.

HERE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ROOT := $(HERE)/..
TOOLS := $(ROOT)/toolchain

OUT ?= $(abspath $(ROOT)/../../build/odf-build)
OBJ := $(OUT)/odf-build-obj
ifeq ($(OS),Windows_NT)
  EXE := .exe
else
  EXE :=
endif

CFLAGS ?= -O2
LDFLAGS ?=

# the tools are third-party code: no warnings, the aliasing rules they were written for
TOOLFLAGS := $(CFLAGS) -w -fno-strict-aliasing -DODF_BUNDLED \
  -Wno-implicit-function-declaration -Wno-implicit-int -Wno-int-conversion -Wno-incompatible-pointer-types

CPP_SRC := $(wildcard $(TOOLS)/lcc/cpp/*.c)
RCC_SRC := $(wildcard $(TOOLS)/lcc/src/*.c)
ASM_SRC := $(TOOLS)/asm/q3asm.c $(TOOLS)/asm/cmdlib.c

OBJECTS := $(patsubst $(TOOLS)/lcc/cpp/%.c,$(OBJ)/cpp/%.o,$(CPP_SRC)) \
           $(patsubst $(TOOLS)/lcc/src/%.c,$(OBJ)/rcc/%.o,$(RCC_SRC)) \
           $(patsubst $(TOOLS)/asm/%.c,$(OBJ)/asm/%.o,$(ASM_SRC)) \
           $(OBJ)/odf-build.o $(OBJ)/sdkdata.o

# the files the program carries; they are written out when it builds a plugin (odf_lib.c, the libraries odf_json.[ch] ...)
SDKFILES := $(wildcard $(ROOT)/include/*) $(wildcard $(ROOT)/sdk/libc/*.h) $(wildcard $(ROOT)/sdk/odf_*.c $(ROOT)/sdk/odf_*.h)

.PHONY: all clean

all: $(OUT)/odf-build$(EXE)

$(OUT)/odf-build$(EXE): $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

$(OBJ)/odf-build.o: $(HERE)/odf-build.c
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -Wall -Wextra -c -o $@ $<

$(OBJ)/mkembed$(EXE): $(HERE)/mkembed.c
	@mkdir -p $(OBJ)
	$(CC) -O1 -o $@ $<

$(OBJ)/sdkdata.c: $(OBJ)/mkembed$(EXE) $(SDKFILES)
	$(OBJ)/mkembed$(EXE) $@ $(ROOT) $(SDKFILES)

$(OBJ)/sdkdata.o: $(OBJ)/sdkdata.c
	$(CC) $(CFLAGS) -c -o $@ $<

# every tool has its own global names (see the names_ files)
$(OBJ)/cpp/%.o: $(TOOLS)/lcc/cpp/%.c $(wildcard $(TOOLS)/lcc/cpp/*.h) $(TOOLS)/names_cpp.h
	@mkdir -p $(dir $@)
	$(CC) $(TOOLFLAGS) -include $(TOOLS)/names_cpp.h -c -o $@ $<

$(OBJ)/rcc/%.o: $(TOOLS)/lcc/src/%.c $(wildcard $(TOOLS)/lcc/src/*.h) $(TOOLS)/names_rcc.h
	@mkdir -p $(dir $@)
	$(CC) $(TOOLFLAGS) -include $(TOOLS)/names_rcc.h -I$(TOOLS)/lcc/src -c -o $@ $<

$(OBJ)/asm/%.o: $(TOOLS)/asm/%.c $(wildcard $(TOOLS)/asm/*.h) $(TOOLS)/names_asm.h
	@mkdir -p $(dir $@)
	$(CC) $(TOOLFLAGS) -include $(TOOLS)/names_asm.h -c -o $@ $<

clean:
	rm -rf $(OBJ) $(OUT)/odf-build$(EXE)
