// This file is a part of Julia. License is MIT: http://julialang.org/license

#ifndef JULIA_H
#define JULIA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "options.h"

#include "libsupport.h"
#include <stdint.h>
#include <string.h>

#include "htable.h"
#include "arraylist.h"

#include <setjmp.h>
#ifndef _OS_WINDOWS_
#  define jl_jmp_buf sigjmp_buf
#  if defined(_CPU_ARM_)
#    define MAX_ALIGN 8
#  else
#    define MAX_ALIGN sizeof(void*)
#  endif
#else
#  define jl_jmp_buf jmp_buf
#  include <malloc.h> //for _resetstkoflw
#  define MAX_ALIGN 8
#endif

#ifdef _P64
#define NWORDS(sz) (((sz)+7)>>3)
#else
#define NWORDS(sz) (((sz)+3)>>2)
#endif

#if __GNUC__
#define NORETURN __attribute__ ((noreturn))
#else
#define NORETURN
#endif

#define container_of(ptr, type, member) \
    ((type *) ((char *)(ptr) - offsetof(type, member)))

#ifdef _MSC_VER
#if _WIN64
#define JL_ATTRIBUTE_ALIGN_PTRSIZE(x) __declspec(align(8)) x
#else
#define JL_ATTRIBUTE_ALIGN_PTRSIZE(x) __declspec(align(4)) x
#endif
#elif __GNUC__
#define JL_ATTRIBUTE_ALIGN_PTRSIZE(x) x __attribute__ ((aligned (sizeof(void*))))
#else
#define JL_ATTRIBUTE_ALIGN_PTRSIZE(x)
#endif

// threading ------------------------------------------------------------------

// WARNING: Threading support is incomplete.  Changing the 1 to a 0 will break Julia.
// Nonetheless, we define JL_THREAD and use it to give advanced notice to maintainers
// of what eventual threading support will change.
#if 1
// Definition for compiling non-thread-safe Julia.
#  define JL_THREAD
#elif !defined(_OS_WINDOWS_)
// Definition for compiling Julia on platforms with GCC __thread.
#  define JL_THREAD __thread
#else
// Definition for compiling Julia on Windows
#  define JL_THREAD __declspec(thread)
#endif

// core data types ------------------------------------------------------------

#ifndef _COMPILER_MICROSOFT_
#define JL_DATA_TYPE \
    struct _jl_value_t *fieldptr0[0];
#else
#define JL_DATA_TYPE
#endif

typedef struct _jl_value_t {
    JL_DATA_TYPE
    struct _jl_value_t *fieldptr[];
} jl_value_t;

typedef struct {
    union {
        jl_value_t *type;
        uintptr_t type_bits;
        struct {
            uintptr_t gc_bits:2;
#ifdef OVERLAP_SVEC_LEN
#ifdef _P64
            uintptr_t unmarked:50;
#else
#error OVERLAP_SVEC_LEN requires 64-bit pointers
#endif
            uintptr_t length:12;
#endif
        };
    };
    jl_value_t value;
} jl_taggedvalue_t;

#define jl_astaggedvalue__MACRO(v) container_of((v),jl_taggedvalue_t,value)
#define jl_typeof__MACRO(v) ((jl_value_t*)(jl_astaggedvalue__MACRO(v)->type_bits&~(size_t)3))
#define jl_astaggedvalue jl_astaggedvalue__MACRO
#define jl_typeof jl_typeof__MACRO
//#define jl_set_typeof(v,t) (jl_astaggedvalue(v)->type = (jl_value_t*)(t))
static inline void jl_set_typeof(void *v, void *t)
{
    jl_taggedvalue_t *tag = jl_astaggedvalue(v);
    tag->type = (jl_value_t*)t;
}
#define jl_typeis(v,t) (jl_typeof(v)==(jl_value_t*)(t))

typedef struct _jl_sym_t {
    JL_DATA_TYPE
    struct _jl_sym_t *left;
    struct _jl_sym_t *right;
    uptrint_t hash;    // precomputed hash value
    JL_ATTRIBUTE_ALIGN_PTRSIZE(char name[]);
} jl_sym_t;

typedef struct _jl_gensym_t {
    JL_DATA_TYPE
    ssize_t id;
} jl_gensym_t;

typedef struct {
    JL_DATA_TYPE
#ifndef OVERLAP_SVEC_LEN
    size_t length;
#endif
    jl_value_t *data[];
} jl_svec_t;

typedef struct {
    JL_DATA_TYPE
    void *data;
#ifdef STORE_ARRAY_LEN
    size_t length;
#endif
    union {
        struct {
            /*
              how - allocation style
              0 = data is inlined, or a foreign pointer we don't manage
              1 = julia-allocated buffer that needs to be marked
              2 = malloc-allocated pointer this array object manages
              3 = has a pointer to the Array that owns the data
            */
            unsigned short how:2;
            unsigned short ndims:10;
            unsigned short pooled:1;
            unsigned short ptrarray:1;  // representation is pointer array
            unsigned short isshared:1;  // data is shared by multiple Arrays
            unsigned short isaligned:1; // data allocated with memalign
        };
        unsigned short flags;
    };
    uint16_t elsize;
    uint32_t offset;  // for 1-d only. does not need to get big.
    size_t nrows;
    union {
        // 1d
        size_t maxsize;
        // Nd
        size_t ncols;
    };
    // other dim sizes go here for ndims > 2

    // followed by alignment padding and inline data, or owner pointer
} jl_array_t;
// compute # of extra words needed to store dimensions
STATIC_INLINE int jl_array_ndimwords(uint32_t ndims)
{
    return (ndims < 3 ? 0 : ndims-2);
}

typedef jl_value_t *(*jl_fptr_t)(jl_value_t*, jl_value_t**, uint32_t);

typedef struct _jl_datatype_t jl_tupletype_t;

typedef struct _jl_lambda_info_t {
    JL_DATA_TYPE
    // this holds the static data for a function:
    // a syntax tree, static parameters, and (if it has been compiled)
    // a function pointer.
    // this is the stuff that's shared among different instantiations
    // (different environments) of a closure.
    jl_value_t *ast;
    // sparams is a vector (symbol, value, symbol, value, ...)
    jl_svec_t *sparams;
    jl_value_t *tfunc;
    jl_sym_t *name;  // for error reporting
    jl_array_t *roots;  // pointers in generated code
    jl_tupletype_t *specTypes;  // argument types this is specialized for
    // a slower-but-works version of this function as a fallback
    struct _jl_function_t *unspecialized;
    // array of all lambda infos with code generated from this one
    jl_array_t *specializations;
    struct _jl_module_t *module;
    struct _jl_lambda_info_t *def;  // original this is specialized from
    jl_value_t *capt;  // captured var info
    jl_sym_t *file;
    int32_t line;
    int8_t inferred;

    // hidden fields:
    // flag telling if inference is running on this function
    // used to avoid infinite recursion
    int8_t inInference : 1;
    int8_t inCompile : 1;
    jl_fptr_t fptr;             // jlcall entry point
    void *functionObject;       // jlcall llvm Function
    void *cFunctionList;        // c callable llvm Functions

    // specialized llvm Function (common core for the other two)
    void *specFunctionObject;
    int32_t functionID; // index that this function will have in the codegen table
    int32_t specFunctionID; // index that this specFunction will have in the codegen table
} jl_lambda_info_t;

typedef struct _jl_function_t {
    JL_DATA_TYPE
    jl_fptr_t fptr;
    jl_value_t *env;
    jl_lambda_info_t *linfo;
} jl_function_t;

typedef struct {
    JL_DATA_TYPE
    jl_svec_t *parameters;
    jl_value_t *body;
} jl_typector_t;

typedef struct {
    JL_DATA_TYPE
    jl_sym_t *name;
    struct _jl_module_t *module;
    jl_svec_t *names;  // field names
    // if this is the name of a parametric type, this field points to the
    // original type.
    // a type alias, for example, might make a type constructor that is
    // not the original.
    jl_value_t *primary;
    jl_svec_t *cache;        // sorted array
    jl_svec_t *linearcache;  // unsorted array
    ptrint_t uid;
} jl_typename_t;

typedef struct {
    JL_DATA_TYPE
    jl_svec_t *types;
} jl_uniontype_t;

typedef struct {
    uint16_t offset;   // offset relative to data start, excluding type tag
    uint16_t size:15;
    uint16_t isptr:1;
} jl_fielddesc_t;

#define JL_FIELD_MAX_OFFSET ((1ul << 16) - 1ul)
#define JL_FIELD_MAX_SIZE ((1ul << 15) - 1ul)

typedef struct _jl_datatype_t {
    JL_DATA_TYPE
    jl_typename_t *name;
    struct _jl_datatype_t *super;
    jl_svec_t *parameters;
    jl_svec_t *types;
    jl_value_t *instance;  // for singletons
    int32_t size;
    uint8_t abstract;
    uint8_t mutabl;
    uint8_t pointerfree;
    // hidden fields:
    uint32_t nfields;
    int32_t ninitialized;
    uint32_t alignment : 31;  // strictest alignment over all fields
    uint32_t haspadding : 1;  // has internal undefined bytes
    uint32_t uid;
    void *struct_decl;  //llvm::Value*
    void *ditype; // llvm::MDNode* to be used as llvm::DIType(ditype)
    jl_fielddesc_t fields[];
} jl_datatype_t;

typedef struct {
    JL_DATA_TYPE
    jl_sym_t *name;
    jl_value_t *lb;   // lower bound
    jl_value_t *ub;   // upper bound
    uint8_t bound;    // part of a constraint environment
} jl_tvar_t;

typedef struct {
    JL_DATA_TYPE
    jl_value_t *value;
} jl_weakref_t;

typedef struct {
    // not first-class
    jl_sym_t *name;
    jl_value_t *value;
    jl_value_t *globalref;  // cached GlobalRef for this binding
    struct _jl_module_t *owner;  // for individual imported bindings
    unsigned constp:1;
    unsigned exportp:1;
    unsigned imported:1;
} jl_binding_t;

typedef struct _jl_module_t {
    JL_DATA_TYPE
    jl_sym_t *name;
    struct _jl_module_t *parent;
    htable_t bindings;
    arraylist_t usings;  // modules with all bindings potentially imported
    jl_array_t *constant_table;
    jl_function_t *call_func;  // cached lookup of `call` within this module
    uint8_t istopmod;
    uint64_t uuid;
} jl_module_t;

typedef struct _jl_methlist_t {
    JL_DATA_TYPE
    jl_tupletype_t *sig;
    int8_t va;
    int8_t isstaged;
    jl_svec_t *tvars;
    jl_function_t *func;
    // cache of specializations of this method for invoke(), i.e.
    // cases where this method was called even though it was not necessarily
    // the most specific for the argument types.
    struct _jl_methtable_t *invokes;
    // TODO: pointer from specialized to original method
    //jl_function_t *orig_method;
    struct _jl_methlist_t *next;
} jl_methlist_t;

typedef struct _jl_methtable_t {
    JL_DATA_TYPE
    jl_sym_t *name;
    jl_methlist_t *defs;
    jl_methlist_t *cache;
    jl_array_t *cache_arg1;
    jl_array_t *cache_targ;
    ptrint_t max_args;  // max # of non-vararg arguments in a signature
    jl_function_t *kwsorter;  // keyword argument sorter function
#ifdef JL_GF_PROFILE
    int ncalls;
#endif
} jl_methtable_t;

typedef struct {
    JL_DATA_TYPE
    jl_sym_t *head;
    jl_array_t *args;
    jl_value_t *etype;
} jl_expr_t;

// constants and type objects -------------------------------------------------

extern DLLEXPORT jl_datatype_t *jl_any_type;
extern DLLEXPORT jl_datatype_t *jl_type_type;
extern DLLEXPORT jl_tvar_t     *jl_typetype_tvar;
extern DLLEXPORT jl_datatype_t *jl_typetype_type;
extern DLLEXPORT jl_value_t    *jl_ANY_flag;
extern DLLEXPORT jl_datatype_t *jl_typename_type;
extern DLLEXPORT jl_datatype_t *jl_typector_type;
extern DLLEXPORT jl_datatype_t *jl_sym_type;
extern DLLEXPORT jl_datatype_t *jl_symbol_type;
extern DLLEXPORT jl_datatype_t *jl_gensym_type;
extern DLLEXPORT jl_datatype_t *jl_simplevector_type;
extern DLLEXPORT jl_typename_t *jl_tuple_typename;
extern DLLEXPORT jl_datatype_t *jl_anytuple_type;
#define jl_tuple_type jl_anytuple_type
extern DLLEXPORT jl_datatype_t *jl_ntuple_type;
extern DLLEXPORT jl_typename_t *jl_ntuple_typename;
extern DLLEXPORT jl_datatype_t *jl_vararg_type;
extern DLLEXPORT jl_datatype_t *jl_tvar_type;
extern DLLEXPORT jl_datatype_t *jl_task_type;

extern DLLEXPORT jl_datatype_t *jl_uniontype_type;
extern DLLEXPORT jl_datatype_t *jl_datatype_type;

extern DLLEXPORT jl_value_t *jl_bottom_type;
extern DLLEXPORT jl_datatype_t *jl_lambda_info_type;
extern DLLEXPORT jl_datatype_t *jl_module_type;
extern DLLEXPORT jl_datatype_t *jl_function_type;
extern DLLEXPORT jl_datatype_t *jl_abstractarray_type;
extern DLLEXPORT jl_datatype_t *jl_densearray_type;
extern DLLEXPORT jl_datatype_t *jl_array_type;
extern DLLEXPORT jl_typename_t *jl_array_typename;
extern DLLEXPORT jl_datatype_t *jl_weakref_type;
extern DLLEXPORT jl_datatype_t *jl_ascii_string_type;
extern DLLEXPORT jl_datatype_t *jl_utf8_string_type;
extern DLLEXPORT jl_datatype_t *jl_errorexception_type;
extern DLLEXPORT jl_datatype_t *jl_argumenterror_type;
extern DLLEXPORT jl_datatype_t *jl_loaderror_type;
extern DLLEXPORT jl_datatype_t *jl_typeerror_type;
extern DLLEXPORT jl_datatype_t *jl_methoderror_type;
extern DLLEXPORT jl_datatype_t *jl_undefvarerror_type;
extern DLLEXPORT jl_value_t *jl_stackovf_exception;
extern DLLEXPORT jl_value_t *jl_memory_exception;
extern DLLEXPORT jl_value_t *jl_readonlymemory_exception;
extern DLLEXPORT jl_value_t *jl_diverror_exception;
extern DLLEXPORT jl_value_t *jl_domain_exception;
extern DLLEXPORT jl_value_t *jl_overflow_exception;
extern DLLEXPORT jl_value_t *jl_inexact_exception;
extern DLLEXPORT jl_value_t *jl_undefref_exception;
extern DLLEXPORT jl_value_t *jl_interrupt_exception;
extern DLLEXPORT jl_datatype_t *jl_boundserror_type;
extern DLLEXPORT jl_value_t *jl_an_empty_cell;

extern DLLEXPORT jl_datatype_t *jl_bool_type;
extern DLLEXPORT jl_datatype_t *jl_char_type;
extern DLLEXPORT jl_datatype_t *jl_int8_type;
extern DLLEXPORT jl_datatype_t *jl_uint8_type;
extern DLLEXPORT jl_datatype_t *jl_int16_type;
extern DLLEXPORT jl_datatype_t *jl_uint16_type;
extern DLLEXPORT jl_datatype_t *jl_int32_type;
extern DLLEXPORT jl_datatype_t *jl_uint32_type;
extern DLLEXPORT jl_datatype_t *jl_int64_type;
extern DLLEXPORT jl_datatype_t *jl_uint64_type;
extern DLLEXPORT jl_datatype_t *jl_float32_type;
extern DLLEXPORT jl_datatype_t *jl_float64_type;
extern DLLEXPORT jl_datatype_t *jl_floatingpoint_type;
extern DLLEXPORT jl_datatype_t *jl_number_type;
extern DLLEXPORT jl_datatype_t *jl_void_type;
extern DLLEXPORT jl_datatype_t *jl_complex_type;
extern DLLEXPORT jl_datatype_t *jl_signed_type;
extern DLLEXPORT jl_datatype_t *jl_voidpointer_type;
extern DLLEXPORT jl_datatype_t *jl_pointer_type;
extern DLLEXPORT jl_datatype_t *jl_ref_type;

extern DLLEXPORT jl_value_t *jl_array_uint8_type;
extern DLLEXPORT jl_value_t *jl_array_any_type;
extern DLLEXPORT jl_value_t *jl_array_symbol_type;
extern DLLEXPORT jl_datatype_t *jl_expr_type;
extern DLLEXPORT jl_datatype_t *jl_symbolnode_type;
extern DLLEXPORT jl_datatype_t *jl_globalref_type;
extern DLLEXPORT jl_datatype_t *jl_linenumbernode_type;
extern DLLEXPORT jl_datatype_t *jl_labelnode_type;
extern DLLEXPORT jl_datatype_t *jl_gotonode_type;
extern DLLEXPORT jl_datatype_t *jl_quotenode_type;
extern DLLEXPORT jl_datatype_t *jl_newvarnode_type;
extern DLLEXPORT jl_datatype_t *jl_topnode_type;
extern DLLEXPORT jl_datatype_t *jl_intrinsic_type;
extern DLLEXPORT jl_datatype_t *jl_methtable_type;
extern DLLEXPORT jl_datatype_t *jl_method_type;
extern DLLEXPORT jl_datatype_t *jl_task_type;

extern DLLEXPORT jl_svec_t *jl_emptysvec;
extern DLLEXPORT jl_value_t *jl_emptytuple;
DLLEXPORT extern jl_value_t *jl_true;
DLLEXPORT extern jl_value_t *jl_false;
DLLEXPORT extern jl_value_t *jl_nothing;

// some important symbols
extern jl_sym_t *call_sym;
extern jl_sym_t *dots_sym;    extern jl_sym_t *vararg_sym;
extern jl_sym_t *quote_sym;   extern jl_sym_t *newvar_sym;
extern jl_sym_t *top_sym;     extern jl_sym_t *dot_sym;
extern jl_sym_t *line_sym;    extern jl_sym_t *toplevel_sym;
extern DLLEXPORT jl_sym_t *jl_incomplete_sym;
extern jl_sym_t *error_sym;   extern jl_sym_t *amp_sym;
extern jl_sym_t *module_sym;  extern jl_sym_t *colons_sym;
extern jl_sym_t *export_sym;  extern jl_sym_t *import_sym;
extern jl_sym_t *importall_sym; extern jl_sym_t *using_sym;
extern jl_sym_t *goto_sym;    extern jl_sym_t *goto_ifnot_sym;
extern jl_sym_t *label_sym;   extern jl_sym_t *return_sym;
extern jl_sym_t *lambda_sym;  extern jl_sym_t *assign_sym;
extern jl_sym_t *null_sym;    extern jl_sym_t *body_sym;
extern jl_sym_t *macro_sym;   extern jl_sym_t *method_sym;
extern jl_sym_t *enter_sym;   extern jl_sym_t *leave_sym;
extern jl_sym_t *exc_sym;     extern jl_sym_t *new_sym;
extern jl_sym_t *static_typeof_sym; extern jl_sym_t *kw_sym;
extern jl_sym_t *const_sym;   extern jl_sym_t *thunk_sym;
extern jl_sym_t *anonymous_sym;  extern jl_sym_t *underscore_sym;
extern jl_sym_t *abstracttype_sym; extern jl_sym_t *bitstype_sym;
extern jl_sym_t *compositetype_sym; extern jl_sym_t *type_goto_sym;
extern jl_sym_t *global_sym;  extern jl_sym_t *tuple_sym;
extern jl_sym_t *boundscheck_sym; extern jl_sym_t *copyast_sym;
extern jl_sym_t *fastmath_sym;
extern jl_sym_t *simdloop_sym; extern jl_sym_t *meta_sym;
extern jl_sym_t *arrow_sym; extern jl_sym_t *inert_sym;

// gc -------------------------------------------------------------------------

typedef struct _jl_gcframe_t {
    size_t nroots;
    struct _jl_gcframe_t *prev;
    // actual roots go here
} jl_gcframe_t;

// NOTE: it is the caller's responsibility to make sure arguments are
// rooted. foo(f(), g()) will not work, and foo can't do anything about it,
// so the caller must do
// jl_value_t *x=NULL, *y=NULL; JL_GC_PUSH(&x, &y);
// x = f(); y = g(); foo(x, y)

extern DLLEXPORT JL_THREAD jl_gcframe_t *jl_pgcstack;

#define JL_GC_PUSH(...)                                                   \
  void *__gc_stkf[] = {(void*)((VA_NARG(__VA_ARGS__)<<1)|1), jl_pgcstack, \
                       __VA_ARGS__};                                      \
  jl_pgcstack = (jl_gcframe_t*)__gc_stkf;

#define JL_GC_PUSH1(arg1)                                                 \
  void *__gc_stkf[] = {(void*)3, jl_pgcstack, arg1};                      \
  jl_pgcstack = (jl_gcframe_t*)__gc_stkf;

#define JL_GC_PUSH2(arg1, arg2)                                           \
  void *__gc_stkf[] = {(void*)5, jl_pgcstack, arg1, arg2};                \
  jl_pgcstack = (jl_gcframe_t*)__gc_stkf;

#define JL_GC_PUSH3(arg1, arg2, arg3)                                     \
  void *__gc_stkf[] = {(void*)7, jl_pgcstack, arg1, arg2, arg3};          \
  jl_pgcstack = (jl_gcframe_t*)__gc_stkf;

#define JL_GC_PUSH4(arg1, arg2, arg3, arg4)                               \
  void *__gc_stkf[] = {(void*)9, jl_pgcstack, arg1, arg2, arg3, arg4};    \
  jl_pgcstack = (jl_gcframe_t*)__gc_stkf;

#define JL_GC_PUSH5(arg1, arg2, arg3, arg4, arg5)                               \
  void *__gc_stkf[] = {(void*)11, jl_pgcstack, arg1, arg2, arg3, arg4, arg5};    \
  jl_pgcstack = (jl_gcframe_t*)__gc_stkf;

#define JL_GC_PUSHARGS(rts_var,n)                               \
  rts_var = ((jl_value_t**)alloca(((n)+2)*sizeof(jl_value_t*)))+2;    \
  ((void**)rts_var)[-2] = (void*)(((size_t)n)<<1);              \
  ((void**)rts_var)[-1] = jl_pgcstack;                          \
  memset((void*)rts_var, 0, (n)*sizeof(jl_value_t*));           \
  jl_pgcstack = (jl_gcframe_t*)&(((void**)rts_var)[-2])

#define JL_GC_POP() (jl_pgcstack = jl_pgcstack->prev)

void jl_gc_init(void);
void jl_gc_setmark(jl_value_t *v);
DLLEXPORT int jl_gc_enable(int on);
DLLEXPORT int jl_gc_is_enabled(void);
DLLEXPORT int64_t jl_gc_total_bytes(void);
DLLEXPORT uint64_t jl_gc_total_hrtime(void);
DLLEXPORT int64_t jl_gc_diff_total_bytes(void);
void jl_gc_sync_total_bytes(void);

DLLEXPORT void jl_gc_collect(int);
DLLEXPORT void jl_gc_preserve(jl_value_t *v);
DLLEXPORT void jl_gc_unpreserve(void);
DLLEXPORT int jl_gc_n_preserved_values(void);

DLLEXPORT void jl_gc_add_finalizer(jl_value_t *v, jl_function_t *f);
DLLEXPORT void jl_finalize(jl_value_t *o);
DLLEXPORT jl_weakref_t *jl_gc_new_weakref(jl_value_t *value);
void jl_gc_free_array(jl_array_t *a);
void jl_gc_track_malloced_array(jl_array_t *a);
void jl_gc_count_allocd(size_t sz);
void jl_gc_run_all_finalizers(void);
DLLEXPORT jl_value_t *jl_gc_alloc_0w(void);
DLLEXPORT jl_value_t *jl_gc_alloc_1w(void);
DLLEXPORT jl_value_t *jl_gc_alloc_2w(void);
DLLEXPORT jl_value_t *jl_gc_alloc_3w(void);
void *allocb(size_t sz);
void *reallocb(void*, size_t);
DLLEXPORT jl_value_t *jl_gc_allocobj(size_t sz);

DLLEXPORT void jl_clear_malloc_data(void);
DLLEXPORT int64_t jl_gc_num_pause(void);
DLLEXPORT int64_t jl_gc_num_full_sweep(void);

// GC write barriers
DLLEXPORT void jl_gc_queue_root(jl_value_t *root); // root isa jl_value_t*
void gc_queue_binding(jl_binding_t *bnd);
void gc_setmark_buf(void *buf, int);

static inline void jl_gc_wb_binding(jl_binding_t *bnd, void *val) // val isa jl_value_t*
{
    if (__unlikely((*((uintptr_t*)bnd-1) & 1) == 1 && (*(uintptr_t*)jl_astaggedvalue(val) & 1) == 0))
        gc_queue_binding(bnd);
}

static inline void jl_gc_wb(void *parent, void *ptr) // parent and ptr isa jl_value_t*
{
    if (__unlikely((*((uintptr_t*)jl_astaggedvalue(parent)) & 1) == 1 &&
                   (*((uintptr_t*)jl_astaggedvalue(ptr)) & 1) == 0))
        jl_gc_queue_root((jl_value_t*)parent);
}

static inline void jl_gc_wb_buf(void *parent, void *bufptr) // parent isa jl_value_t*
{
    // if parent is marked and buf is not
    if (__unlikely((*((uintptr_t*)jl_astaggedvalue(parent)) & 1) == 1))
        //                   (*((uintptr_t*)bufptr) & 3) != 1))
        gc_setmark_buf(bufptr, *(uintptr_t*)jl_astaggedvalue(parent) & 3);
}

static inline void jl_gc_wb_back(void *ptr) // ptr isa jl_value_t*
{
    // if ptr is marked
    if(__unlikely((*((uintptr_t*)jl_astaggedvalue(ptr)) & 1) == 1)) {
        jl_gc_queue_root((jl_value_t*)ptr);
    }
}

DLLEXPORT void *jl_gc_managed_malloc(size_t sz);
DLLEXPORT void *jl_gc_managed_realloc(void *d, size_t sz, size_t oldsz, int isaligned, jl_value_t* owner);

// object accessors -----------------------------------------------------------

#define jl_typeis(v,t) (jl_typeof(v)==(jl_value_t*)(t))

#define jl_svec_len(t)              (((jl_svec_t*)(t))->length)
#define jl_svec_set_len_unsafe(t,n) (((jl_svec_t*)(t))->length=(n))
#define jl_svec_data(t)             (((jl_svec_t*)(t))->data)

STATIC_INLINE jl_value_t *jl_svecref(void *t, size_t i)
{
    assert(jl_typeis(t,jl_simplevector_type));
    assert(i < jl_svec_len(t));
    return jl_svec_data(t)[i];
}
STATIC_INLINE jl_value_t *jl_svecset(void *t, size_t i, void *x)
{
    assert(jl_typeis(t,jl_simplevector_type));
    assert(i < jl_svec_len(t));
    jl_svec_data(t)[i] = (jl_value_t*)x;
    if (x) jl_gc_wb(t, x);
    return (jl_value_t*)x;
}

#ifdef STORE_ARRAY_LEN
#define jl_array_len(a)   (((jl_array_t*)(a))->length)
#else
DLLEXPORT size_t jl_array_len_(jl_array_t *a);
#define jl_array_len(a)   jl_array_len_((jl_array_t*)(a))
#endif
#define jl_array_data(a)  ((void*)((jl_array_t*)(a))->data)
#define jl_array_dim(a,i) ((&((jl_array_t*)(a))->nrows)[i])
#define jl_array_dim0(a)  (((jl_array_t*)(a))->nrows)
#define jl_array_nrows(a) (((jl_array_t*)(a))->nrows)
#define jl_array_ndims(a) ((int32_t)(((jl_array_t*)a)->ndims))
#define jl_array_data_owner_offset(ndims) (offsetof(jl_array_t,ncols) + sizeof(size_t)*(1+jl_array_ndimwords(ndims))) // in bytes
#define jl_array_data_owner(a) (*((jl_value_t**)((char*)a + jl_array_data_owner_offset(jl_array_ndims(a)))))

STATIC_INLINE jl_value_t *jl_cellref(void *a, size_t i)
{
    assert(i < jl_array_len(a));
    return ((jl_value_t**)(jl_array_data(a)))[i];
}
STATIC_INLINE jl_value_t *jl_cellset(void *a, size_t i, void *x)
{
    assert(i < jl_array_len(a));
    ((jl_value_t**)(jl_array_data(a)))[i] = (jl_value_t*)x;
    if (x) {
        if (((jl_array_t*)a)->how == 3) {
            a = jl_array_data_owner(a);
        }
        jl_gc_wb(a, x);
    }
    return (jl_value_t*)x;
}

#define jl_exprarg(e,n) (((jl_value_t**)jl_array_data(((jl_expr_t*)(e))->args))[n])
#define jl_exprargset(e, n, v) jl_cellset(((jl_expr_t*)(e))->args, n, v)
#define jl_expr_nargs(e) jl_array_len(((jl_expr_t*)(e))->args)

#define jl_fieldref(s,i) jl_get_nth_field(((jl_value_t*)s),i)
#define jl_nfields(v)    jl_datatype_nfields(jl_typeof(v))

#define jl_symbolnode_sym(s) ((jl_sym_t*)jl_fieldref(s,0))
#define jl_symbolnode_type(s) (jl_fieldref(s,1))
#define jl_linenode_line(x) (((ptrint_t*)x)[0])
#define jl_labelnode_label(x) (((ptrint_t*)x)[0])
#define jl_gotonode_label(x) (((ptrint_t*)x)[0])
#define jl_globalref_mod(s) ((jl_module_t*)jl_fieldref(s,0))
#define jl_globalref_name(s) ((jl_sym_t*)jl_fieldref(s,1))

#define jl_nparams(t)  jl_svec_len(((jl_datatype_t*)(t))->parameters)
#define jl_tparam0(t)  jl_svecref(((jl_datatype_t*)(t))->parameters, 0)
#define jl_tparam1(t)  jl_svecref(((jl_datatype_t*)(t))->parameters, 1)
#define jl_tparam(t,i) jl_svecref(((jl_datatype_t*)(t))->parameters, i)

#define jl_cell_data(a)   ((jl_value_t**)((jl_array_t*)a)->data)
#define jl_string_data(s) ((char*)((jl_array_t*)(s)->fieldptr[0])->data)
#define jl_iostr_data(s)  ((char*)((jl_array_t*)(s)->fieldptr[0])->data)

#define jl_gf_mtable(f) ((jl_methtable_t*)((jl_function_t*)(f))->env)
#define jl_gf_name(f)   (jl_gf_mtable(f)->name)

// get a pointer to the data in a datatype
#define jl_data_ptr(v)  (((jl_value_t*)v)->fieldptr)

// struct type info
#define jl_field_offset(st,i)  (((jl_datatype_t*)st)->fields[i].offset)
#define jl_field_size(st,i)    (((jl_datatype_t*)st)->fields[i].size)
#define jl_field_isptr(st,i)   (((jl_datatype_t*)st)->fields[i].isptr)
#define jl_field_name(st,i)    (jl_sym_t*)jl_svecref(((jl_datatype_t*)st)->name->names, (i))
#define jl_field_type(st,i)    jl_svecref(((jl_datatype_t*)st)->types, (i))
#define jl_datatype_size(t)    (((jl_datatype_t*)t)->size)
#define jl_datatype_nfields(t) (((jl_datatype_t*)(t))->nfields)

// basic predicates -----------------------------------------------------------
#define jl_is_nothing(v)     (((jl_value_t*)(v)) == ((jl_value_t*)jl_nothing))
#define jl_is_tuple(v)       (((jl_datatype_t*)jl_typeof(v))->name == jl_tuple_typename)
#define jl_is_svec(v)        jl_typeis(v,jl_simplevector_type)
#define jl_is_simplevector(v) jl_is_svec(v)
#define jl_is_datatype(v)    jl_typeis(v,jl_datatype_type)
#define jl_is_pointerfree(t) (((jl_datatype_t*)t)->pointerfree)
#define jl_is_mutable(t)     (((jl_datatype_t*)t)->mutabl)
#define jl_is_mutable_datatype(t) (jl_is_datatype(t) && (((jl_datatype_t*)t)->mutabl))
#define jl_is_immutable(t)   (!((jl_datatype_t*)t)->mutabl)
#define jl_is_immutable_datatype(t) (jl_is_datatype(t) && (!((jl_datatype_t*)t)->mutabl))
#define jl_is_uniontype(v)   jl_typeis(v,jl_uniontype_type)
#define jl_is_typevar(v)     jl_typeis(v,jl_tvar_type)
#define jl_is_typector(v)    jl_typeis(v,jl_typector_type)
#define jl_is_TypeConstructor(v)    jl_typeis(v,jl_typector_type)
#define jl_is_typename(v)    jl_typeis(v,jl_typename_type)
#define jl_is_int8(v)        jl_typeis(v,jl_int8_type)
#define jl_is_int16(v)       jl_typeis(v,jl_int16_type)
#define jl_is_int32(v)       jl_typeis(v,jl_int32_type)
#define jl_is_int64(v)       jl_typeis(v,jl_int64_type)
#define jl_is_uint8(v)       jl_typeis(v,jl_uint8_type)
#define jl_is_uint16(v)      jl_typeis(v,jl_uint16_type)
#define jl_is_uint32(v)      jl_typeis(v,jl_uint32_type)
#define jl_is_uint64(v)      jl_typeis(v,jl_uint64_type)
#define jl_is_float(v)       jl_subtype(v,(jl_value_t*)jl_floatingpoint_type,1)
#define jl_is_floattype(v)   jl_subtype(v,(jl_value_t*)jl_floatingpoint_type,0)
#define jl_is_float32(v)     jl_typeis(v,jl_float32_type)
#define jl_is_float64(v)     jl_typeis(v,jl_float64_type)
#define jl_is_bool(v)        jl_typeis(v,jl_bool_type)
#define jl_is_symbol(v)      jl_typeis(v,jl_sym_type)
#define jl_is_gensym(v)      jl_typeis(v,jl_gensym_type)
#define jl_is_expr(v)        jl_typeis(v,jl_expr_type)
#define jl_is_symbolnode(v)  jl_typeis(v,jl_symbolnode_type)
#define jl_is_globalref(v)   jl_typeis(v,jl_globalref_type)
#define jl_is_labelnode(v)   jl_typeis(v,jl_labelnode_type)
#define jl_is_gotonode(v)    jl_typeis(v,jl_gotonode_type)
#define jl_is_quotenode(v)   jl_typeis(v,jl_quotenode_type)
#define jl_is_newvarnode(v)  jl_typeis(v,jl_newvarnode_type)
#define jl_is_topnode(v)     jl_typeis(v,jl_topnode_type)
#define jl_is_linenode(v)    jl_typeis(v,jl_linenumbernode_type)
#define jl_is_lambda_info(v) jl_typeis(v,jl_lambda_info_type)
#define jl_is_module(v)      jl_typeis(v,jl_module_type)
#define jl_is_mtable(v)      jl_typeis(v,jl_methtable_type)
#define jl_is_task(v)        jl_typeis(v,jl_task_type)
#define jl_is_func(v)        jl_typeis(v,jl_function_type)
#define jl_is_function(v)    jl_is_func(v)
#define jl_is_ascii_string(v) jl_typeis(v,jl_ascii_string_type)
#define jl_is_utf8_string(v) jl_typeis(v,jl_utf8_string_type)
#define jl_is_byte_string(v) (jl_is_ascii_string(v) || jl_is_utf8_string(v))
#define jl_is_cpointer(v)    jl_is_cpointer_type(jl_typeof(v))
#define jl_is_pointer(v)     jl_is_cpointer_type(jl_typeof(v))
#define jl_is_gf(f)          (((jl_function_t*)(f))->fptr==jl_apply_generic)

STATIC_INLINE int jl_is_bitstype(void *v)
{
    return (jl_is_datatype(v) && jl_is_immutable(v) &&
            jl_datatype_nfields(v) == 0 &&
            !((jl_datatype_t*)(v))->abstract &&
            ((jl_datatype_t*)(v))->size > 0);
}

STATIC_INLINE int jl_is_structtype(void *v)
{
    return (jl_is_datatype(v) &&
            (jl_datatype_nfields(v) > 0 ||
             ((jl_datatype_t*)(v))->size == 0) &&
            !((jl_datatype_t*)(v))->abstract);
}

STATIC_INLINE int jl_isbits(void *t)   // corresponding to isbits() in julia
{
    return (jl_is_datatype(t) && !((jl_datatype_t*)t)->mutabl &&
            ((jl_datatype_t*)t)->pointerfree && !((jl_datatype_t*)t)->abstract);
}

STATIC_INLINE int jl_is_datatype_singleton(jl_datatype_t *d)
{
    return (d->instance != NULL ||
            (!d->abstract && d->size == 0 && d != jl_sym_type && d->name != jl_array_typename &&
             (d->name->names == jl_emptysvec || !d->mutabl)));
}

STATIC_INLINE int jl_is_abstracttype(void *v)
{
    return (jl_is_datatype(v) && ((jl_datatype_t*)(v))->abstract);
}

STATIC_INLINE int jl_is_array_type(void *t)
{
    return (jl_is_datatype(t) &&
            ((jl_datatype_t*)(t))->name == jl_array_typename);
}

STATIC_INLINE int jl_is_array(void *v)
{
    jl_value_t *t = jl_typeof(v);
    return jl_is_array_type(t);
}

STATIC_INLINE int jl_is_cpointer_type(jl_value_t *t)
{
    return (jl_is_datatype(t) &&
            ((jl_datatype_t*)(t))->name == jl_pointer_type->name);
}

STATIC_INLINE int jl_is_abstract_ref_type(jl_value_t *t)
{
    return (jl_is_datatype(t) &&
            ((jl_datatype_t*)(t))->name == jl_ref_type->name);
}

STATIC_INLINE jl_value_t *jl_is_ref_type(jl_value_t *t)
{
    if (!jl_is_datatype(t)) return 0;
    jl_datatype_t *dt = (jl_datatype_t*)t;
    while (dt != jl_any_type && dt->name != dt->super->name) {
        if (dt->name == jl_ref_type->name)
            return (jl_value_t*)dt;
        dt = dt->super;
    }
    return 0;
}

STATIC_INLINE int jl_is_tuple_type(void *t)
{
    return (jl_is_datatype(t) &&
            ((jl_datatype_t*)(t))->name == jl_tuple_typename);
}

STATIC_INLINE int jl_is_vararg_type(jl_value_t *v)
{
    return (jl_is_datatype(v) &&
            ((jl_datatype_t*)(v))->name == jl_vararg_type->name);
}

STATIC_INLINE int jl_is_va_tuple(jl_datatype_t *t)
{
    size_t l = jl_svec_len(t->parameters);
    return (l>0 && jl_is_vararg_type(jl_tparam(t,l-1)));
}

STATIC_INLINE int jl_is_ntuple_type(jl_value_t *v)
{
    return (jl_is_datatype(v) &&
            ((jl_datatype_t*)v)->name == jl_ntuple_typename);
}

STATIC_INLINE int jl_is_type_type(jl_value_t *v)
{
    return (jl_is_datatype(v) &&
            ((jl_datatype_t*)(v))->name == jl_type_type->name);
}

// object identity
DLLEXPORT int jl_egal(jl_value_t *a, jl_value_t *b);
DLLEXPORT uptrint_t jl_object_id(jl_value_t *v);

// type predicates and basic operations
int jl_is_type(jl_value_t *v);
DLLEXPORT int jl_is_leaf_type(jl_value_t *v);
DLLEXPORT int jl_has_typevars(jl_value_t *v);
DLLEXPORT int jl_subtype(jl_value_t *a, jl_value_t *b, int ta);
int jl_type_morespecific(jl_value_t *a, jl_value_t *b);
DLLEXPORT int jl_types_equal(jl_value_t *a, jl_value_t *b);
DLLEXPORT jl_value_t *jl_type_union(jl_svec_t *types);
jl_value_t *jl_type_union_v(jl_value_t **ts, size_t n);
jl_value_t *jl_type_intersection_matching(jl_value_t *a, jl_value_t *b,
                                          jl_svec_t **penv, jl_svec_t *tvars);
DLLEXPORT jl_value_t *jl_type_intersection(jl_value_t *a, jl_value_t *b);
DLLEXPORT int jl_args_morespecific(jl_value_t *a, jl_value_t *b);
DLLEXPORT const char *jl_typename_str(jl_value_t *v);
DLLEXPORT const char *jl_typeof_str(jl_value_t *v);

// type constructors
DLLEXPORT jl_typename_t *jl_new_typename(jl_sym_t *name);
DLLEXPORT jl_tvar_t *jl_new_typevar(jl_sym_t *name,jl_value_t *lb,jl_value_t *ub);
jl_typector_t *jl_new_type_ctor(jl_svec_t *params, jl_value_t *body);
DLLEXPORT jl_value_t *jl_apply_type(jl_value_t *tc, jl_svec_t *params);
DLLEXPORT jl_tupletype_t *jl_apply_tuple_type(jl_svec_t *params);
DLLEXPORT jl_tupletype_t *jl_apply_tuple_type_v(jl_value_t **p, size_t np);
jl_value_t *jl_apply_type_(jl_value_t *tc, jl_value_t **params, size_t n);
jl_value_t *jl_instantiate_type_with(jl_value_t *t, jl_value_t **env, size_t n);
jl_datatype_t *jl_new_abstracttype(jl_value_t *name, jl_datatype_t *super,
                                   jl_svec_t *parameters);
DLLEXPORT jl_datatype_t *jl_new_uninitialized_datatype(size_t nfields);
DLLEXPORT jl_datatype_t *jl_new_datatype(jl_sym_t *name, jl_datatype_t *super,
                                         jl_svec_t *parameters,
                                         jl_svec_t *fnames, jl_svec_t *ftypes,
                                         int abstract, int mutabl, int ninitialized);
DLLEXPORT jl_datatype_t *jl_new_bitstype(jl_value_t *name, jl_datatype_t *super,
                                         jl_svec_t *parameters, size_t nbits);
jl_datatype_t *jl_wrap_Type(jl_value_t *t);  // x -> Type{x}
jl_datatype_t *jl_wrap_vararg(jl_value_t *t);

// constructors
DLLEXPORT jl_value_t *jl_new_bits(jl_value_t *bt, void *data);
void jl_assign_bits(void *dest, jl_value_t *bits);
DLLEXPORT jl_value_t *jl_new_struct(jl_datatype_t *type, ...);
DLLEXPORT jl_value_t *jl_new_structv(jl_datatype_t *type, jl_value_t **args, uint32_t na);
DLLEXPORT jl_value_t *jl_new_struct_uninit(jl_datatype_t *type);
DLLEXPORT jl_function_t *jl_new_closure(jl_fptr_t proc, jl_value_t *env,
                                        jl_lambda_info_t *li);
DLLEXPORT jl_lambda_info_t *jl_new_lambda_info(jl_value_t *ast, jl_svec_t *sparams);
DLLEXPORT jl_svec_t *jl_svec(size_t n, ...);
DLLEXPORT jl_svec_t *jl_svec1(void *a);
DLLEXPORT jl_svec_t *jl_svec2(void *a, void *b);
DLLEXPORT jl_svec_t *jl_alloc_svec(size_t n);
DLLEXPORT jl_svec_t *jl_alloc_svec_uninit(size_t n);
DLLEXPORT jl_svec_t *jl_svec_append(jl_svec_t *a, jl_svec_t *b);
jl_svec_t *jl_svec_copy(jl_svec_t *a);
DLLEXPORT jl_svec_t *jl_svec_fill(size_t n, jl_value_t *x);
DLLEXPORT jl_value_t *jl_tupletype_fill(size_t n, jl_value_t *v);
DLLEXPORT jl_sym_t *jl_symbol(const char *str);
DLLEXPORT jl_sym_t *jl_symbol_lookup(const char *str);
DLLEXPORT jl_sym_t *jl_symbol_n(const char *str, int32_t len);
DLLEXPORT jl_sym_t *jl_gensym(void);
DLLEXPORT jl_sym_t *jl_tagged_gensym(const char *str, int32_t len);
DLLEXPORT jl_sym_t *jl_get_root_symbol(void);
jl_expr_t *jl_exprn(jl_sym_t *head, size_t n);
jl_function_t *jl_new_generic_function(jl_sym_t *name);
void jl_add_method(jl_function_t *gf, jl_tupletype_t *types, jl_function_t *meth,
                   jl_svec_t *tvars, int8_t isstaged);
DLLEXPORT jl_value_t *jl_generic_function_def(jl_sym_t *name, jl_value_t **bp, jl_value_t *bp_owner,
                                              jl_binding_t *bnd);
DLLEXPORT jl_value_t *jl_method_def(jl_sym_t *name, jl_value_t **bp, jl_value_t *bp_owner, jl_binding_t *bnd,
                                    jl_svec_t *argtypes, jl_function_t *f, jl_value_t *isstaged,
                                    jl_value_t *call_func, int iskw);
DLLEXPORT jl_value_t *jl_box_bool(int8_t x);
DLLEXPORT jl_value_t *jl_box_int8(int8_t x);
DLLEXPORT jl_value_t *jl_box_uint8(uint8_t x);
DLLEXPORT jl_value_t *jl_box_int16(int16_t x);
DLLEXPORT jl_value_t *jl_box_uint16(uint16_t x);
DLLEXPORT jl_value_t *jl_box_int32(int32_t x);
DLLEXPORT jl_value_t *jl_box_uint32(uint32_t x);
DLLEXPORT jl_value_t *jl_box_char(uint32_t x);
DLLEXPORT jl_value_t *jl_box_int64(int64_t x);
DLLEXPORT jl_value_t *jl_box_uint64(uint64_t x);
DLLEXPORT jl_value_t *jl_box_float32(float x);
DLLEXPORT jl_value_t *jl_box_float64(double x);
DLLEXPORT jl_value_t *jl_box_voidpointer(void *x);
DLLEXPORT jl_value_t *jl_box_gensym(size_t x);
DLLEXPORT jl_value_t *jl_box8 (jl_datatype_t *t, int8_t  x);
DLLEXPORT jl_value_t *jl_box16(jl_datatype_t *t, int16_t x);
DLLEXPORT jl_value_t *jl_box32(jl_datatype_t *t, int32_t x);
DLLEXPORT jl_value_t *jl_box64(jl_datatype_t *t, int64_t x);
DLLEXPORT int8_t jl_unbox_bool(jl_value_t *v);
DLLEXPORT int8_t jl_unbox_int8(jl_value_t *v);
DLLEXPORT uint8_t jl_unbox_uint8(jl_value_t *v);
DLLEXPORT int16_t jl_unbox_int16(jl_value_t *v);
DLLEXPORT uint16_t jl_unbox_uint16(jl_value_t *v);
DLLEXPORT int32_t jl_unbox_int32(jl_value_t *v);
DLLEXPORT uint32_t jl_unbox_uint32(jl_value_t *v);
DLLEXPORT int64_t jl_unbox_int64(jl_value_t *v);
DLLEXPORT uint64_t jl_unbox_uint64(jl_value_t *v);
DLLEXPORT float jl_unbox_float32(jl_value_t *v);
DLLEXPORT double jl_unbox_float64(jl_value_t *v);
DLLEXPORT void *jl_unbox_voidpointer(jl_value_t *v);
DLLEXPORT ssize_t jl_unbox_gensym(jl_value_t *v);

DLLEXPORT int jl_get_size(jl_value_t *val, size_t *pnt);

#ifdef _P64
#define jl_box_long(x)   jl_box_int64(x)
#define jl_box_ulong(x)  jl_box_uint64(x)
#define jl_unbox_long(x) jl_unbox_int64(x)
#define jl_is_long(x)    jl_is_int64(x)
#define jl_long_type     jl_int64_type
#else
#define jl_box_long(x)   jl_box_int32(x)
#define jl_box_ulong(x)  jl_box_uint32(x)
#define jl_unbox_long(x) jl_unbox_int32(x)
#define jl_is_long(x)    jl_is_int32(x)
#define jl_long_type     jl_int32_type
#endif

// structs
DLLEXPORT int         jl_field_index(jl_datatype_t *t, jl_sym_t *fld, int err);
DLLEXPORT jl_value_t *jl_get_nth_field(jl_value_t *v, size_t i);
DLLEXPORT jl_value_t *jl_get_nth_field_checked(jl_value_t *v, size_t i);
DLLEXPORT void        jl_set_nth_field(jl_value_t *v, size_t i, jl_value_t *rhs);
DLLEXPORT int         jl_field_isdefined(jl_value_t *v, size_t i);
DLLEXPORT jl_value_t *jl_get_field(jl_value_t *o, char *fld);
DLLEXPORT jl_value_t *jl_value_ptr(jl_value_t *a);

// arrays

DLLEXPORT jl_array_t *jl_new_array(jl_value_t *atype, jl_value_t *dims);
DLLEXPORT jl_array_t *jl_new_arrayv(jl_value_t *atype, ...);
DLLEXPORT jl_array_t *jl_reshape_array(jl_value_t *atype, jl_array_t *data,
                                       jl_value_t *dims);
DLLEXPORT jl_array_t *jl_ptr_to_array_1d(jl_value_t *atype, void *data,
                                         size_t nel, int own_buffer);
DLLEXPORT jl_array_t *jl_ptr_to_array(jl_value_t *atype, void *data,
                                      jl_value_t *dims, int own_buffer);
int jl_array_store_unboxed(jl_value_t *el_type);

DLLEXPORT jl_array_t *jl_alloc_array_1d(jl_value_t *atype, size_t nr);
DLLEXPORT jl_array_t *jl_alloc_array_2d(jl_value_t *atype, size_t nr, size_t nc);
DLLEXPORT jl_array_t *jl_alloc_array_3d(jl_value_t *atype, size_t nr, size_t nc,
                                        size_t z);
DLLEXPORT jl_array_t *jl_pchar_to_array(const char *str, size_t len);
DLLEXPORT jl_value_t *jl_pchar_to_string(const char *str, size_t len);
DLLEXPORT jl_value_t *jl_cstr_to_string(const char *str);
DLLEXPORT jl_value_t *jl_array_to_string(jl_array_t *a);
DLLEXPORT jl_array_t *jl_alloc_cell_1d(size_t n);
DLLEXPORT jl_value_t *jl_arrayref(jl_array_t *a, size_t i);  // 0-indexed
DLLEXPORT void jl_arrayset(jl_array_t *a, jl_value_t *v, size_t i);  // 0-indexed
DLLEXPORT void jl_arrayunset(jl_array_t *a, size_t i);  // 0-indexed
int jl_array_isdefined(jl_value_t **args, int nargs);
DLLEXPORT void jl_array_grow_end(jl_array_t *a, size_t inc);
DLLEXPORT void jl_array_del_end(jl_array_t *a, size_t dec);
DLLEXPORT void jl_array_grow_beg(jl_array_t *a, size_t inc);
DLLEXPORT void jl_array_del_beg(jl_array_t *a, size_t dec);
DLLEXPORT void jl_array_sizehint(jl_array_t *a, size_t sz);
DLLEXPORT void jl_cell_1d_push(jl_array_t *a, jl_value_t *item);
DLLEXPORT jl_value_t *jl_apply_array_type(jl_datatype_t *type, size_t dim);
// property access
DLLEXPORT void *jl_array_ptr(jl_array_t *a);
DLLEXPORT void *jl_array_eltype(jl_value_t *a);
DLLEXPORT int jl_array_rank(jl_value_t *a);
DLLEXPORT size_t jl_array_size(jl_value_t *a, int d);

// strings
DLLEXPORT const char *jl_bytestring_ptr(jl_value_t *s);

// modules and global variables
extern DLLEXPORT jl_module_t *jl_main_module;
extern DLLEXPORT jl_module_t *jl_internal_main_module;
extern DLLEXPORT jl_module_t *jl_core_module;
extern DLLEXPORT jl_module_t *jl_base_module;
extern DLLEXPORT jl_module_t *jl_top_module;
extern DLLEXPORT jl_module_t *jl_current_module;
DLLEXPORT jl_module_t *jl_new_module(jl_sym_t *name);
// get binding for reading
DLLEXPORT jl_binding_t *jl_get_binding(jl_module_t *m, jl_sym_t *var);
DLLEXPORT jl_binding_t *jl_get_binding_or_error(jl_module_t *m, jl_sym_t *var);
DLLEXPORT jl_value_t *jl_module_globalref(jl_module_t *m, jl_sym_t *var);
// get binding for assignment
DLLEXPORT jl_binding_t *jl_get_binding_wr(jl_module_t *m, jl_sym_t *var);
DLLEXPORT jl_binding_t *jl_get_binding_for_method_def(jl_module_t *m, jl_sym_t *var);
DLLEXPORT int jl_boundp(jl_module_t *m, jl_sym_t *var);
DLLEXPORT int jl_defines_or_exports_p(jl_module_t *m, jl_sym_t *var);
DLLEXPORT int jl_binding_resolved_p(jl_module_t *m, jl_sym_t *var);
DLLEXPORT int jl_is_const(jl_module_t *m, jl_sym_t *var);
DLLEXPORT jl_value_t *jl_get_global(jl_module_t *m, jl_sym_t *var);
DLLEXPORT void jl_set_global(jl_module_t *m, jl_sym_t *var, jl_value_t *val);
DLLEXPORT void jl_set_const(jl_module_t *m, jl_sym_t *var, jl_value_t *val);
DLLEXPORT void jl_checked_assignment(jl_binding_t *b, jl_value_t *rhs);
DLLEXPORT void jl_declare_constant(jl_binding_t *b);
DLLEXPORT void jl_module_using(jl_module_t *to, jl_module_t *from);
DLLEXPORT void jl_module_use(jl_module_t *to, jl_module_t *from, jl_sym_t *s);
DLLEXPORT void jl_module_import(jl_module_t *to, jl_module_t *from, jl_sym_t *s);
DLLEXPORT void jl_module_importall(jl_module_t *to, jl_module_t *from);
DLLEXPORT void jl_module_export(jl_module_t *from, jl_sym_t *s);
DLLEXPORT int jl_is_imported(jl_module_t *m, jl_sym_t *s);
DLLEXPORT jl_module_t *jl_new_main_module(void);
DLLEXPORT void jl_add_standard_imports(jl_module_t *m);
STATIC_INLINE jl_function_t *jl_get_function(jl_module_t *m, const char *name)
{
    return  (jl_function_t*) jl_get_global(m, jl_symbol(name));
}
DLLEXPORT void jl_module_run_initializer(jl_module_t *m);
jl_function_t *jl_module_call_func(jl_module_t *m);
int jl_is_submodule(jl_module_t *child, jl_module_t *parent);

// eq hash tables
DLLEXPORT jl_array_t *jl_eqtable_put(jl_array_t *h, void *key, void *val);
DLLEXPORT jl_value_t *jl_eqtable_get(jl_array_t *h, void *key, jl_value_t *deflt);

// system information
DLLEXPORT int jl_errno(void);
DLLEXPORT void jl_set_errno(int e);
DLLEXPORT int32_t jl_stat(const char *path, char *statbuf);
DLLEXPORT int jl_cpu_cores(void);
DLLEXPORT long jl_getpagesize(void);
DLLEXPORT long jl_getallocationgranularity(void);
DLLEXPORT int jl_is_debugbuild(void);

// environment entries
DLLEXPORT jl_value_t *jl_environ(int i);

// throwing common exceptions
DLLEXPORT void NORETURN jl_error(const char *str);
DLLEXPORT void NORETURN jl_errorf(const char *fmt, ...);
DLLEXPORT void NORETURN jl_exceptionf(jl_datatype_t *ty, const char *fmt, ...);
DLLEXPORT void NORETURN jl_too_few_args(const char *fname, int min);
DLLEXPORT void NORETURN jl_too_many_args(const char *fname, int max);
DLLEXPORT void NORETURN jl_type_error(const char *fname, jl_value_t *expected, jl_value_t *got);
DLLEXPORT void NORETURN jl_type_error_rt(const char *fname, const char *context,
                                jl_value_t *ty, jl_value_t *got);
DLLEXPORT void NORETURN jl_type_error_rt_line(const char *fname, const char *context,
                                     jl_value_t *ty, jl_value_t *got, int line);
DLLEXPORT void NORETURN jl_undefined_var_error(jl_sym_t *var);
DLLEXPORT void NORETURN jl_bounds_error(jl_value_t *v, jl_value_t *t);
DLLEXPORT void NORETURN jl_bounds_error_v(jl_value_t *v, jl_value_t **idxs, size_t nidxs);
DLLEXPORT void NORETURN jl_bounds_error_int(jl_value_t *v, size_t i);
DLLEXPORT void NORETURN jl_bounds_error_tuple_int(jl_value_t **v, size_t nv, size_t i);
DLLEXPORT void NORETURN jl_bounds_error_unboxed_int(void *v, jl_value_t *vt, size_t i);
DLLEXPORT void NORETURN jl_bounds_error_ints(jl_value_t *v, size_t *idxs, size_t nidxs);
DLLEXPORT jl_value_t *jl_exception_occurred(void);
DLLEXPORT void jl_exception_clear(void);

#define JL_NARGS(fname, min, max)                               \
    if (nargs < min) jl_too_few_args(#fname, min);              \
    else if (nargs > max) jl_too_many_args(#fname, max);

#define JL_NARGSV(fname, min)                           \
    if (nargs < min) jl_too_few_args(#fname, min);

#define JL_TYPECHK(fname, type, v)                                      \
    if (!jl_is_##type(v)) {                                             \
        jl_type_error(#fname, (jl_value_t*)jl_##type##_type, (v));      \
    }
#define JL_TYPECHKS(fname, type, v)                                     \
    if (!jl_is_##type(v)) {                                             \
        jl_type_error(fname, (jl_value_t*)jl_##type##_type, (v));       \
    }

// initialization functions
typedef enum {
    JL_IMAGE_CWD = 0,
    JL_IMAGE_JULIA_HOME = 1,
    //JL_IMAGE_LIBJULIA = 2,
} JL_IMAGE_SEARCH;
DLLEXPORT void julia_init(JL_IMAGE_SEARCH rel);
DLLEXPORT void jl_init(const char *julia_home_dir);
DLLEXPORT void jl_init_with_image(const char *julia_home_dir, const char *image_relative_path);
DLLEXPORT int jl_is_initialized(void);
DLLEXPORT int julia_trampoline(int argc, const char *argv[], int (*pmain)(int ac,char *av[]));
DLLEXPORT void jl_atexit_hook(void);
DLLEXPORT void NORETURN jl_exit(int status);

DLLEXPORT void jl_preload_sysimg_so(const char *fname);
DLLEXPORT ios_t *jl_create_system_image();
DLLEXPORT void jl_save_system_image(const char *fname);
DLLEXPORT void jl_restore_system_image(const char *fname);
DLLEXPORT void jl_restore_system_image_data(const char *buf, size_t len);
DLLEXPORT int jl_save_new_module(const char *fname, jl_module_t *mod);
DLLEXPORT jl_module_t *jl_restore_new_module(const char *fname);
void jl_init_restored_modules();

// front end interface
DLLEXPORT jl_value_t *jl_parse_input_line(const char *str, size_t len);
DLLEXPORT jl_value_t *jl_parse_string(const char *str, size_t len,
                                      int pos0, int greedy);
DLLEXPORT int jl_parse_depwarn(int warn);
int jl_start_parsing_file(const char *fname);
void jl_stop_parsing(void);
jl_value_t *jl_parse_next(void);
DLLEXPORT jl_value_t *jl_load_file_string(const char *text, size_t len,
                                          char *filename, size_t namelen);
DLLEXPORT jl_value_t *jl_expand(jl_value_t *expr);
jl_lambda_info_t *jl_wrap_expr(jl_value_t *expr);
DLLEXPORT void *jl_eval_string(const char *str);

// external libraries
enum JL_RTLD_CONSTANT {
     JL_RTLD_LOCAL=1U,
     JL_RTLD_GLOBAL=2U,
     JL_RTLD_LAZY=4U,
     JL_RTLD_NOW=8U,
     /* Linux/glibc and MacOS X: */
     JL_RTLD_NODELETE=16U,
     JL_RTLD_NOLOAD=32U,
     /* Linux/glibc: */
     JL_RTLD_DEEPBIND=64U,
     /* MacOS X 10.5+: */
     JL_RTLD_FIRST=128U
};
#define JL_RTLD_DEFAULT (JL_RTLD_LAZY | JL_RTLD_DEEPBIND)

typedef void *jl_uv_libhandle; // uv_lib_t* (avoid uv.h dependency)
DLLEXPORT jl_uv_libhandle jl_load_dynamic_library(const char *fname, unsigned flags);
DLLEXPORT jl_uv_libhandle jl_load_dynamic_library_e(const char *fname, unsigned flags);
DLLEXPORT void *jl_dlsym_e(jl_uv_libhandle handle, const char *symbol);
DLLEXPORT void *jl_dlsym(jl_uv_libhandle handle, const char *symbol);
DLLEXPORT int jl_uv_dlopen(const char *filename, jl_uv_libhandle lib, unsigned flags);
char *jl_dlfind_win32(const char *name);
DLLEXPORT int add_library_mapping(char *lib, void *hnd);

#if defined(__linux__) || defined(__FreeBSD__)
DLLEXPORT const char *jl_lookup_soname(const char *pfx, size_t n);
#endif

// compiler
void jl_compile(jl_function_t *f);
DLLEXPORT jl_value_t *jl_toplevel_eval(jl_value_t *v);
DLLEXPORT jl_value_t *jl_toplevel_eval_in(jl_module_t *m, jl_value_t *ex);
jl_value_t *jl_eval_global_var(jl_module_t *m, jl_sym_t *e);
DLLEXPORT jl_value_t *jl_load(const char *fname);
jl_value_t *jl_parse_eval_all(const char *fname, size_t len);
jl_value_t *jl_interpret_toplevel_thunk(jl_lambda_info_t *lam);
jl_value_t *jl_interpret_toplevel_thunk_with(jl_lambda_info_t *lam,
                                             jl_value_t **loc, size_t nl);
jl_value_t *jl_interpret_toplevel_expr(jl_value_t *e);
DLLEXPORT jl_value_t *jl_interpret_toplevel_expr_in(jl_module_t *m, jl_value_t *e,
                                                    jl_value_t **locals, size_t nl);
jl_value_t *jl_static_eval(jl_value_t *ex, void *ctx_, jl_module_t *mod,
                           jl_value_t *sp, jl_expr_t *ast, int sparams, int allow_alloc);
int jl_is_toplevel_only_expr(jl_value_t *e);
DLLEXPORT jl_module_t *jl_base_relative_to(jl_module_t *m);
void jl_type_infer(jl_lambda_info_t *li, jl_tupletype_t *argtypes, jl_lambda_info_t *def);

jl_function_t *jl_method_lookup_by_type(jl_methtable_t *mt, jl_tupletype_t *types,
                                        int cache, int inexact);
jl_function_t *jl_method_lookup(jl_methtable_t *mt, jl_value_t **args, size_t nargs, int cache);
jl_value_t *jl_gf_invoke(jl_function_t *gf, jl_tupletype_t *types,
                         jl_value_t **args, size_t nargs);

// AST access
jl_array_t *jl_lam_args(jl_expr_t *l);
jl_array_t *jl_lam_vinfo(jl_expr_t *l);
jl_array_t *jl_lam_capt(jl_expr_t *l);
jl_value_t *jl_lam_gensyms(jl_expr_t *l);
jl_array_t *jl_lam_staticparams(jl_expr_t *l);
jl_sym_t *jl_lam_argname(jl_lambda_info_t *li, int i);
int jl_lam_vars_captured(jl_expr_t *ast);
jl_expr_t *jl_lam_body(jl_expr_t *l);
DLLEXPORT jl_value_t *jl_ast_rettype(jl_lambda_info_t *li, jl_value_t *ast);
jl_sym_t *jl_decl_var(jl_value_t *ex);
DLLEXPORT int jl_is_rest_arg(jl_value_t *ex);

DLLEXPORT jl_value_t *jl_prepare_ast(jl_lambda_info_t *li, jl_svec_t *sparams);
DLLEXPORT jl_value_t *jl_copy_ast(jl_value_t *expr);

DLLEXPORT jl_value_t *jl_compress_ast(jl_lambda_info_t *li, jl_value_t *ast);
DLLEXPORT jl_value_t *jl_uncompress_ast(jl_lambda_info_t *li, jl_value_t *data);

DLLEXPORT int jl_is_operator(char *sym);
DLLEXPORT int jl_operator_precedence(char *sym);

STATIC_INLINE int jl_vinfo_capt(jl_array_t *vi)
{
    return (jl_unbox_long(jl_cellref(vi,2))&1)!=0;
}

STATIC_INLINE int jl_vinfo_assigned(jl_array_t *vi)
{
    return (jl_unbox_long(jl_cellref(vi,2))&2)!=0;
}

STATIC_INLINE int jl_vinfo_assigned_inner(jl_array_t *vi)
{
    return (jl_unbox_long(jl_cellref(vi,2))&4)!=0;
}

STATIC_INLINE int jl_vinfo_sa(jl_array_t *vi)
{
    return (jl_unbox_long(jl_cellref(vi,2))&16)!=0;
}

STATIC_INLINE int jl_vinfo_usedundef(jl_array_t *vi)
{
    return (jl_unbox_long(jl_cellref(vi,2))&32)!=0;
}

// calling into julia ---------------------------------------------------------

STATIC_INLINE
jl_value_t *jl_apply(jl_function_t *f, jl_value_t **args, uint32_t nargs)
{
    return f->fptr((jl_value_t*)f, args, nargs);
}

DLLEXPORT jl_value_t *jl_call(jl_function_t *f, jl_value_t **args, int32_t nargs);
DLLEXPORT jl_value_t *jl_call0(jl_function_t *f);
DLLEXPORT jl_value_t *jl_call1(jl_function_t *f, jl_value_t *a);
DLLEXPORT jl_value_t *jl_call2(jl_function_t *f, jl_value_t *a, jl_value_t *b);
DLLEXPORT jl_value_t *jl_call3(jl_function_t *f, jl_value_t *a, jl_value_t *b, jl_value_t *c);

// interfacing with Task runtime
DLLEXPORT void jl_yield();

// async signal handling ------------------------------------------------------

#include <signal.h>

DLLEXPORT extern volatile sig_atomic_t jl_signal_pending;
DLLEXPORT extern volatile sig_atomic_t jl_defer_signal;

#define JL_SIGATOMIC_BEGIN() (jl_defer_signal++)
#define JL_SIGATOMIC_END()                                      \
    do {                                                        \
        jl_defer_signal--;                                      \
        if (jl_defer_signal == 0 && jl_signal_pending != 0) {   \
            jl_signal_pending = 0;                              \
            jl_sigint_action();                                 \
        }                                                       \
    } while(0)

DLLEXPORT void jl_sigint_action(void);
DLLEXPORT void restore_signals(void);
DLLEXPORT void jl_install_sigint_handler(void);
DLLEXPORT void jl_sigatomic_begin(void);
DLLEXPORT void jl_sigatomic_end(void);
void jl_install_default_signal_handlers(void);


// tasks and exceptions -------------------------------------------------------

// info describing an exception handler
typedef struct _jl_handler_t {
    jl_jmp_buf eh_ctx;
    jl_gcframe_t *gcstack;
    struct _jl_handler_t *prev;
} jl_handler_t;

typedef struct _jl_task_t {
    JL_DATA_TYPE
    struct _jl_task_t *parent;
    struct _jl_task_t *last;
    jl_value_t *tls;
    jl_sym_t *state;
    jl_value_t *consumers;
    jl_value_t *donenotify;
    jl_value_t *result;
    jl_value_t *exception;
    jl_function_t *start;
    jl_jmp_buf ctx;
#ifndef COPY_STACKS
    void *stack;
#endif
    size_t bufsz;
    void *stkbuf;
    size_t ssize;

    // current exception handler
    jl_handler_t *eh;
    // saved gc stack top for context switches
    jl_gcframe_t *gcstack;
    // current module, or NULL if this task has not set one
    jl_module_t *current_module;
} jl_task_t;

extern DLLEXPORT JL_THREAD jl_task_t * volatile jl_current_task;
extern DLLEXPORT JL_THREAD jl_task_t *jl_root_task;
extern DLLEXPORT JL_THREAD jl_value_t *jl_exception_in_transit;

DLLEXPORT jl_task_t *jl_new_task(jl_function_t *start, size_t ssize);
DLLEXPORT jl_value_t *jl_switchto(jl_task_t *t, jl_value_t *arg);
DLLEXPORT void NORETURN jl_throw(jl_value_t *e);
DLLEXPORT void NORETURN jl_throw_with_superfluous_argument(jl_value_t *e, int);
DLLEXPORT void NORETURN jl_rethrow(void);
DLLEXPORT void NORETURN jl_rethrow_other(jl_value_t *e);

STATIC_INLINE void jl_eh_restore_state(jl_handler_t *eh)
{
    JL_SIGATOMIC_BEGIN();
    jl_current_task->eh = eh->prev;
    jl_pgcstack = eh->gcstack;
    JL_SIGATOMIC_END();
}

DLLEXPORT void jl_enter_handler(jl_handler_t *eh);
DLLEXPORT void jl_pop_handler(int n);

#if defined(_OS_WINDOWS_)
#if defined(_COMPILER_MINGW_)
int __attribute__ ((__nothrow__,__returns_twice__)) jl_setjmp(jmp_buf _Buf);
__declspec(noreturn) __attribute__ ((__nothrow__)) void jl_longjmp(jmp_buf _Buf,int _Value);
#else
int jl_setjmp(jmp_buf _Buf);
void jl_longjmp(jmp_buf _Buf,int _Value);
#endif
#define jl_setjmp_f jl_setjmp
#define jl_setjmp_name "jl_setjmp"
#define jl_setjmp(a,b) jl_setjmp(a)
#define jl_longjmp(a,b) jl_longjmp(a,b)
#else
// determine actual entry point name
#if defined(sigsetjmp)
#define jl_setjmp_f    __sigsetjmp
#define jl_setjmp_name "__sigsetjmp"
#else
#define jl_setjmp_f    sigsetjmp
#define jl_setjmp_name "sigsetjmp"
#endif
#define jl_setjmp(a,b) sigsetjmp(a,b)
#define jl_longjmp(a,b) siglongjmp(a,b)
#endif

#define JL_TRY                                                    \
    int i__tr, i__ca; jl_handler_t __eh;                          \
    jl_enter_handler(&__eh);                                      \
    if (!jl_setjmp(__eh.eh_ctx,0))                                \
        for (i__tr=1; i__tr; i__tr=0, jl_eh_restore_state(&__eh))

#define JL_EH_POP() jl_eh_restore_state(&__eh)

#ifdef _OS_WINDOWS_
#define JL_CATCH                                                \
    else                                                        \
        for (i__ca=1, jl_eh_restore_state(&__eh); i__ca; i__ca=0) \
            if (((jl_exception_in_transit==jl_stackovf_exception) && _resetstkoflw()) || 1)
#else
#define JL_CATCH                                                \
    else                                                        \
        for (i__ca=1, jl_eh_restore_state(&__eh); i__ca; i__ca=0)
#endif

// I/O system -----------------------------------------------------------------

#define JL_STREAM uv_stream_t
#define JL_STDOUT jl_uv_stdout
#define JL_STDERR jl_uv_stderr
#define JL_STDIN  jl_uv_stdin

DLLEXPORT void jl_run_event_loop(uv_loop_t *loop);
DLLEXPORT int jl_run_once(uv_loop_t *loop);
DLLEXPORT int jl_process_events(uv_loop_t *loop);

DLLEXPORT uv_loop_t *jl_global_event_loop(void);

DLLEXPORT uv_pipe_t *jl_make_pipe(int writable, int julia_only, jl_value_t *julia_struct);
DLLEXPORT void jl_close_uv(uv_handle_t *handle);

DLLEXPORT int32_t jl_start_reading(uv_stream_t *handle);

DLLEXPORT void jl_callback(void *callback);

DLLEXPORT uv_async_t *jl_make_async(uv_loop_t *loop, jl_value_t *julia_struct);
DLLEXPORT void jl_async_send(uv_async_t *handle);
DLLEXPORT uv_idle_t * jl_make_idle(uv_loop_t *loop, jl_value_t *julia_struct);
DLLEXPORT int jl_idle_start(uv_idle_t *idle);
DLLEXPORT int jl_idle_stop(uv_idle_t *idle);

DLLEXPORT uv_timer_t *jl_make_timer(uv_loop_t *loop, jl_value_t *julia_struct);
DLLEXPORT int jl_timer_stop(uv_timer_t *timer);

DLLEXPORT uv_tcp_t *jl_tcp_init(uv_loop_t *loop);
DLLEXPORT int jl_tcp_bind(uv_tcp_t *handle, uint16_t port, uint32_t host, unsigned int flags);

DLLEXPORT int jl_sizeof_ios_t(void);

#ifdef _OS_WINDOWS_
DLLEXPORT struct tm* localtime_r(const time_t *t, struct tm *tm);
#endif

DLLEXPORT jl_array_t *jl_takebuf_array(ios_t *s);
DLLEXPORT jl_value_t *jl_takebuf_string(ios_t *s);
DLLEXPORT void *jl_takebuf_raw(ios_t *s);
DLLEXPORT jl_value_t *jl_readuntil(ios_t *s, uint8_t delim);

typedef struct {
    void *data;
    uv_loop_t *loop;
    uv_handle_type type;
    uv_file file;
} jl_uv_file_t;

#ifdef __GNUC__
#define _JL_FORMAT_ATTR(type, str, arg) \
    __attribute__((format(type, str, arg)))
#else
#define _JL_FORMAT_ATTR(type, str, arg)
#endif

DLLEXPORT int jl_printf(uv_stream_t *s, const char *format, ...)
    _JL_FORMAT_ATTR(printf, 2, 3);
DLLEXPORT int jl_vprintf(uv_stream_t *s, const char *format, va_list args)
    _JL_FORMAT_ATTR(printf, 2, 0);
DLLEXPORT void jl_safe_printf(const char *str, ...)
    _JL_FORMAT_ATTR(printf, 1, 2);

extern DLLEXPORT JL_STREAM *JL_STDIN;
extern DLLEXPORT JL_STREAM *JL_STDOUT;
extern DLLEXPORT JL_STREAM *JL_STDERR;

DLLEXPORT JL_STREAM *jl_stdout_stream(void);
DLLEXPORT JL_STREAM *jl_stdin_stream(void);
DLLEXPORT JL_STREAM *jl_stderr_stream(void);

// showing and std streams
DLLEXPORT void jl_show(jl_value_t *stream, jl_value_t *v);
DLLEXPORT void jl_flush_cstdio(void);
DLLEXPORT jl_value_t *jl_stdout_obj(void);
DLLEXPORT jl_value_t *jl_stderr_obj(void);
DLLEXPORT size_t jl_static_show(JL_STREAM *out, jl_value_t *v);
DLLEXPORT size_t jl_static_show_func_sig(JL_STREAM *s, jl_value_t *type);
DLLEXPORT void jlbacktrace(void);

#if defined(GC_FINAL_STATS)
void jl_print_gc_stats(JL_STREAM *s);
#endif

// debugging
void show_execution_point(char *filename, int lno);

// julia options -----------------------------------------------------------
// NOTE: This struct needs to be kept in sync with JLOptions type in base/options.jl
typedef struct {
    int8_t quiet;
    const char *julia_home;
    const char *julia_bin;
    const char *eval;
    const char *print;
    const char *postboot;
    const char *load;
    const char *image_file;
    const char *cpu_target;
    int32_t nprocs;
    const char *machinefile;
    int8_t isinteractive;
    int8_t color;
    int8_t historyfile;
    int8_t startupfile;
    int8_t compile_enabled;
    int8_t code_coverage;
    int8_t malloc_log;
    int8_t opt_level;
    int8_t check_bounds;
    int8_t depwarn;
    int8_t can_inline;
    int8_t fast_math;
    int8_t worker;
    int8_t handle_signals;
    int8_t use_precompiled;
    const char *bindto;
    const char *outputbc;
    const char *outputo;
    const char *outputji;
} jl_options_t;

extern DLLEXPORT jl_options_t jl_options;

DLLEXPORT int jl_generating_output();

// Settings for code_coverage and malloc_log
// NOTE: if these numbers change, test/cmdlineargs.jl will have to be updated
#define JL_LOG_NONE 0
#define JL_LOG_USER 1
#define JL_LOG_ALL  2

#define JL_OPTIONS_CHECK_BOUNDS_DEFAULT 0
#define JL_OPTIONS_CHECK_BOUNDS_ON 1
#define JL_OPTIONS_CHECK_BOUNDS_OFF 2

#define JL_OPTIONS_COMPILE_DEFAULT 1
#define JL_OPTIONS_COMPILE_OFF 0
#define JL_OPTIONS_COMPILE_ON  1
#define JL_OPTIONS_COMPILE_ALL 2

#define JL_OPTIONS_COLOR_ON 1
#define JL_OPTIONS_COLOR_OFF 2

#define JL_OPTIONS_HISTORYFILE_ON 1
#define JL_OPTIONS_HISTORYFILE_OFF 0

#define JL_OPTIONS_STARTUPFILE_ON 1
#define JL_OPTIONS_STARTUPFILE_OFF 2

#define JL_OPTIONS_DEPWARN_OFF 0
#define JL_OPTIONS_DEPWARN_ON 1
#define JL_OPTIONS_DEPWARN_ERROR 2

#define JL_OPTIONS_FAST_MATH_ON 1
#define JL_OPTIONS_FAST_MATH_OFF 2
#define JL_OPTIONS_FAST_MATH_DEFAULT 0

#define JL_OPTIONS_HANDLE_SIGNALS_ON 1
#define JL_OPTIONS_HANDLE_SIGNALS_OFF 0

#define JL_OPTIONS_USE_PRECOMPILED_YES 1
#define JL_OPTIONS_USE_PRECOMPILED_NO 0

// Version information
#include "julia_version.h"

DLLEXPORT extern int jl_ver_major(void);
DLLEXPORT extern int jl_ver_minor(void);
DLLEXPORT extern int jl_ver_patch(void);
DLLEXPORT extern int jl_ver_is_release(void);
DLLEXPORT extern const char* jl_ver_string(void);

// nullable struct representations
typedef struct {
    uint8_t isnull;
    double value;
} jl_nullable_float64_t;

typedef struct {
    uint8_t isnull;
    float value;
} jl_nullable_float32_t;

#ifdef __cplusplus
}
#endif

#endif
