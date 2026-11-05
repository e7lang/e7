// Special lowerings selected by the checker and implemented by codegen.
#pragma once

namespace e7 {

enum Intrinsic : int {
  IN_NONE = 0,
  IN_FUNC_EXPR = 1,       // Arrow: 'function' expression
  // calls
  IN_NATIVE,              // call e->native with receiver (if any) + args
  IN_NATIVE_NORECV,       // call e->native with args only
  IN_DIRECT,              // direct call to e->callee (function / static method)
  IN_CLOSURE_CALL,        // call a function-typed value
  IN_METHOD,              // virtual method call (e->vslot)
  IN_IFACE_METHOD,        // interface method call by name
  IN_SUPER_CTOR,          // super(...)
  IN_SUPER_METHOD,        // super.m(...)
  IN_FIELD_CALL,          // call a function-typed field
  // value conversions
  IN_TO_INT,              // u32(x) etc.
  IN_NUMBER,              // Number(x)
  IN_STRING,              // String(x)
  IN_BOOLEAN,             // Boolean(x)
  IN_BIGINT,              // BigInt(x)
  IN_TOSTRING,            // x.toString()
  IN_INTOP,               // rotl32 / clz32 / ... (opcode in e->ival)
  // arrays
  IN_ARR_PUSH, IN_ARR_LEN, IN_ARR_MAP, IN_ARR_FILTER, IN_ARR_FOREACH, IN_ARR_REDUCE, IN_ARR_REDUCE_RIGHT,
  IN_ARR_SOME, IN_ARR_EVERY, IN_ARR_FIND, IN_ARR_FINDINDEX, IN_ARR_FINDLAST, IN_ARR_FINDLASTINDEX,
  IN_ARR_FROM_LENGTH,     // Array.from({length: n}, fn)
  IN_ARRAY_FROM,          // Array.from(iterable) / [...x]
  IN_NEW_ARRAY,           // new Array<T>(n)
  IN_MAP_FOREACH, IN_SET_FOREACH,
  IN_BYTES_FOREACH,
  IN_STR_CONCAT,
  IN_CONSOLE_LOG,
  IN_MATH_MINMAX,         // Math.min / Math.max with N args
  IN_HYPOT,
  IN_CONST_NUM,           // constant number (e->num)
  IN_CONST_STR,           // constant string (e->str)
  IN_ENUM_MEMBER,
  IN_STATIC_FIELD,        // ClassName.field (global var in e->var)
  IN_TYPEOF_CONST,
  IN_JSON_PARSE,
  IN_JSON_TRY_PARSE,      // tryParse(text) as T | null (e7:json)
  IN_OBJECT_KEYS,
  IN_ABORT,
  IN_ERROR_IS,  // x instanceof TypeError (and the other Error names): compares the Error's name
  IN_ARR_LENGTH_SET,
  IN_DATE_CMP,           // relational comparison of two Dates (by time value)
  // accessors (get x() / set x(v)), compiled to method calls; Member nodes
  IN_ACCESSOR,           // instance accessor of class e->cls (virtual: "get x" / "set x" vtable slots)
  IN_STATIC_ACCESSOR,    // static accessor of class e->cls (direct call)
  IN_SUPER_ACCESSOR,     // super.x: accessor of the parent class e->cls (direct call, this = e->var)
  IN_IFACE_ACCESSOR,     // accessor declared by an interface (call by name)
  IN_PRIVATE_METHOD,     // this.#m(...): non-virtual call of e->callee
  IN_ABSTRACT_PROP,      // abstract property read/written through an abstract class type (by name)
  // arrays / objects / strings lowered to loops
  IN_ARR_FLATMAP, IN_ARR_FLAT,
  IN_OBJECT_VALUES, IN_OBJECT_ENTRIES,
  IN_OBJECT_ASSIGN,      // Object.assign(target, ...sources) (target not a literal)
  IN_STR_REPLACE_FN,     // s.replace(pattern, fn) / s.replaceAll(pattern, fn) (e->bval: all)
  // Arrow nodes
  IN_OBJ_METHOD = 1000,  // method shorthand in an object literal (own 'this': the object)
};

}  // namespace e7
