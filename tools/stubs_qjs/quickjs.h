// stub minimo SOLO para comprobar la sintaxis de jsx.cpp sin QuickJS real (no es la API completa)
#pragma once
#include <cstddef>
#include <cstdint>
struct JSRuntime; struct JSContext;
struct JSValue { int64_t v; };
typedef JSValue JSValueConst;
typedef JSValue JSCFunction(JSContext*, JSValueConst, int, JSValueConst*);
struct JSMallocFunctions { void* (*js_calloc)(void*, size_t, size_t); void* (*js_malloc)(void*, size_t); void (*js_free)(void*, void*); void* (*js_realloc)(void*, void*, size_t); size_t (*js_malloc_usable_size)(const void*); };
#define JS_NULL (JSValue{2})
#define JS_UNDEFINED (JSValue{3})
#define JS_EVAL_TYPE_GLOBAL 0
JSValue JS_Call(JSContext*, JSValueConst, JSValueConst, int, JSValueConst*);
JSValue JS_DupValue(JSContext*, JSValueConst);
JSValue JS_Eval(JSContext*, const char*, size_t, const char*, int);
int JS_ExecutePendingJob(JSRuntime*, JSContext**);
void JS_FreeCString(JSContext*, const char*); void JS_FreeContext(JSContext*); void JS_FreeRuntime(JSRuntime*); void JS_FreeValue(JSContext*, JSValue);
uint8_t* JS_GetArrayBuffer(JSContext*, size_t*, JSValueConst);
JSValue JS_GetException(JSContext*); JSValue JS_GetGlobalObject(JSContext*); JSValue JS_GetPropertyStr(JSContext*, JSValueConst, const char*);
bool JS_IsException(JSValueConst); bool JS_IsFunction(JSContext*, JSValueConst); bool JS_IsNull(JSValueConst); bool JS_IsUndefined(JSValueConst);
JSValue JS_NewArray(JSContext*); JSValue JS_NewArrayBufferCopy(JSContext*, const uint8_t*, size_t); JSValue JS_NewBool(JSContext*, bool);
JSValue JS_NewCFunction(JSContext*, JSCFunction*, const char*, int); JSContext* JS_NewContext(JSRuntime*); JSValue JS_NewFloat64(JSContext*, double);
JSValue JS_NewInt32(JSContext*, int32_t); JSValue JS_NewObject(JSContext*); JSRuntime* JS_NewRuntime2(const JSMallocFunctions*, void*);
JSValue JS_NewStringLen(JSContext*, const char*, size_t);
void JS_SetInterruptHandler(JSRuntime*, int (*)(JSContext*, void*), void*); void JS_SetMaxStackSize(JSRuntime*, size_t); void JS_SetMemoryLimit(JSRuntime*, size_t);
int JS_SetPropertyStr(JSContext*, JSValueConst, const char*, JSValue); int JS_SetPropertyUint32(JSContext*, JSValueConst, uint32_t, JSValue);
int JS_ToBool(JSContext*, JSValueConst); const char* JS_ToCStringLen(JSContext*, size_t*, JSValueConst);
int JS_ToFloat64(JSContext*, double*, JSValueConst); int JS_ToInt32(JSContext*, int32_t*, JSValueConst);
