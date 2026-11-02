# The WebAssembly runtime (WAMR) that runs the plugins: which files, with which options.
# Included by the Makefile of the engine and by the tests. Set WAMR (the core directory) before including.
#
# The interpreter only: no threads, no WASI (a plugin has no system access), no AOT/JIT, memory bounds checked in software.

WAMR_DEFS = -DBH_PLATFORM_ODF \
  -DWASM_ENABLE_INTERP=1 -DWASM_ENABLE_FAST_INTERP=1 -DWASM_ENABLE_AOT=0 -DWASM_ENABLE_JIT=0 -DWASM_ENABLE_FAST_JIT=0 \
  -DWASM_ENABLE_LIBC_BUILTIN=0 -DWASM_ENABLE_LIBC_WASI=0 -DWASM_ENABLE_LIBC_UVWASI=0 -DWASM_ENABLE_MULTI_MODULE=0 \
  -DWASM_ENABLE_MINI_LOADER=0 -DWASM_ENABLE_BULK_MEMORY=1 -DWASM_ENABLE_REF_TYPES=1 -DWASM_ENABLE_SIMD=0 \
  -DWASM_ENABLE_INSTRUCTION_METERING=1 -DWASM_ENABLE_LOAD_CUSTOM_SECTION=1 -DWASM_ENABLE_SHARED_MEMORY=0 \
  -DWASM_ENABLE_THREAD_MGR=0 -DWASM_ENABLE_TAIL_CALL=0 -DWASM_ENABLE_GC=0 -DWASM_ENABLE_EXCE_HANDLING=0 \
  -DWASM_DISABLE_HW_BOUND_CHECK=1 -DWASM_DISABLE_STACK_HW_BOUND_CHECK=1 -DWASM_DISABLE_WAKEUP_BLOCKING_OP=1 \
  -DWASM_ENABLE_WAMR_COMPILER=0 -DWASM_ENABLE_DEBUG_INTERP=0 -DWASM_ENABLE_CUSTOM_NAME_SECTION=0 \
  -DWASM_ENABLE_SPEC_TEST=0 -DWASM_ENABLE_WASM_CACHE=0 -DWASM_ENABLE_QUICK_AOT_ENTRY=0 \
  -DWASM_ENABLE_MEMORY_PROFILING=0 -DWASM_ENABLE_PERF_PROFILING=0 -DWASM_ENABLE_DUMP_CALL_STACK=0 \
  -DWASM_ENABLE_LAZY_JIT=0 -DWASM_ENABLE_GLOBAL_HEAP_POOL=0 -DWASM_RUNTIME_API_EXTERN= \
  -DBH_MALLOC=wasm_runtime_malloc -DBH_FREE=wasm_runtime_free

WAMR_INC = -I$(WAMR)/shared/platform/odf -I$(WAMR)/shared/platform/include -I$(WAMR)/shared/utils \
  -I$(WAMR)/shared/mem-alloc -I$(WAMR)/shared/mem-alloc/ems -I$(WAMR)/iwasm/include \
  -I$(WAMR)/iwasm/common -I$(WAMR)/iwasm/interpreter -I$(WAMR)

# WebAssembly needs exact IEEE floats (NaN, infinity), wrapping integers and no aliasing assumptions, whatever the engine builds with
WAMR_SAFE = -fno-fast-math -fno-finite-math-only -fwrapv -fno-strict-aliasing

# the source files, without .c, relative to the core directory
WAMR_NAMES = \
  iwasm/common/wasm_application iwasm/common/wasm_blocking_op iwasm/common/wasm_c_api iwasm/common/wasm_exec_env \
  iwasm/common/wasm_loader_common iwasm/common/wasm_memory iwasm/common/wasm_native iwasm/common/wasm_runtime_common \
  iwasm/common/wasm_shared_memory iwasm/interpreter/wasm_loader iwasm/interpreter/wasm_runtime iwasm/interpreter/wasm_interp_fast \
  shared/mem-alloc/mem_alloc shared/mem-alloc/ems/ems_alloc shared/mem-alloc/ems/ems_gc shared/mem-alloc/ems/ems_hmu \
  shared/mem-alloc/ems/ems_kfc shared/utils/bh_assert shared/utils/bh_bitmap shared/utils/bh_common shared/utils/bh_hashmap \
  shared/utils/bh_leb128 shared/utils/bh_list shared/utils/bh_log shared/utils/bh_queue shared/utils/bh_vector \
  shared/utils/runtime_timer shared/platform/odf/platform_odf
WAMR_BASENAMES = $(notdir $(WAMR_NAMES))
WAMR_DIRS = iwasm/common iwasm/interpreter shared/mem-alloc shared/mem-alloc/ems shared/utils shared/platform/odf

# the assembly that calls the engine from the interpreter, by CPU and system
ifeq ($(WAMR_CPU),x86_64)
  WAMR_TARGET = -DBUILD_TARGET_X86_64
  ifeq ($(WAMR_WINDOWS),1)
    WAMR_INVOKE = invokeNative_mingw_x64
  else
    WAMR_INVOKE = invokeNative_em64
  endif
else
ifeq ($(WAMR_CPU),aarch64)
  WAMR_TARGET = -DBUILD_TARGET_AARCH64
  WAMR_INVOKE = invokeNative_aarch64
else
  $(error The plugin runtime supports x86_64 and aarch64 only (CPU $(WAMR_CPU)))
endif
endif
