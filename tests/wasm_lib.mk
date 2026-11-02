# Builds the WebAssembly runtime of the plugins (code/wasm) into $(OUT)/libwamr.a for the native tests.
#   make -f ../wasm_lib.mk OUT=build            (run from a test directory)

MOUNT_DIR ?= ../../code
OUT ?= build
CC ?= gcc

WAMR = $(MOUNT_DIR)/wasm/wamr/core
UNAME := $(shell uname -s)
WAMR_CPU := $(shell uname -m | sed -e 's/amd64/x86_64/' -e 's/arm64/aarch64/')
ifneq (,$(findstring MINGW,$(UNAME))$(findstring MSYS,$(UNAME)))
  WAMR_WINDOWS = 1
endif
include $(MOUNT_DIR)/wasm/wamr.mk

OBJS = $(addprefix $(OUT)/wasm/,$(addsuffix .o,$(WAMR_BASENAMES))) $(OUT)/wasm/$(WAMR_INVOKE).o
WCFLAGS = -O2 -w $(WAMR_TARGET) $(WAMR_DEFS) $(WAMR_INC) $(WAMR_SAFE)

$(OUT)/libwamr.a: $(OBJS)
	rm -f $@
	ar rcs $@ $(OBJS)

define RULE
$$(OUT)/wasm/%.o: $$(WAMR)/$(1)/%.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(WCFLAGS) -c -o $$@ $$<
endef
$(foreach d,$(WAMR_DIRS),$(eval $(call RULE,$(d))))

$(OUT)/wasm/%.o: $(WAMR)/iwasm/common/arch/%.s
	@mkdir -p $(dir $@)
	$(CC) -c -o $@ $<
