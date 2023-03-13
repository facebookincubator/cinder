#pragma once

/*
 * Macros for marking components in core CPython we currently export for the
 * CinderVM module. This includes things added by Cinder and things which
 * already existed but which weren't public.
 *
 * The intent is grepping for "CiAPI" reveals everything the CinderVM module may
 * depend on in the core CPython code. Eliminating all of these is one of the
 * prerequisites for CinderVM being compatible with non-Cinder Python.
 */

// These function the same as PyAPI_* - exporting symbols for use in .so's etc.
#define CiAPI_FUNC(RTYPE) __attribute__ ((visibility ("default"))) RTYPE
#ifdef __clang__
#  ifdef __cplusplus
#    define CiAPI_DATA(RTYPE) __attribute__ ((visibility ("default"))) extern "C" RTYPE
#  else
#    define CiAPI_DATA(RTYPE) __attribute__ ((visibility ("default"))) extern RTYPE
#  endif
#else
#  ifdef __cplusplus
#    define CiAPI_DATA(RTYPE) extern "C" __attribute__ ((visibility ("default")))RTYPE
#  else
#    define CiAPI_DATA(RTYPE) extern __attribute__ ((visibility ("default"))) RTYPE
#  endif
#endif

// Clang seems to (always?) make symbols for static inline functions.
#ifdef __clang__
#  define CiAPI_STATIC_INLINE_FUNC(RTYPE) static inline __attribute__ ((visibility ("default"))) RTYPE
#else
#  define CiAPI_STATIC_INLINE_FUNC(RTYPE) static inline RTYPE
#endif
