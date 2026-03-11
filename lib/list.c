/*
 * lib/list.c – Generic intrusive doubly-linked list
 *
 * All list operations are defined as static inline functions in
 * include/lib/list.h.  This translation unit exists so that the build
 * system can produce list.o without complaints, and to serve as a home
 * for any future non-inline helpers.
 */
#include "lib/list.h"