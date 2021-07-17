/* Generated by Cython 0.29.21 */

#ifndef __PYX_HAVE__recordclass___linkedlist
#define __PYX_HAVE__recordclass___linkedlist

#include "Python.h"
struct LinkedItem;
struct LinkedList;

/* "recordclass/_linkedlist.pyx":4
 * 
 * @cython.no_gc
 * cdef public class linkeditem[object LinkedItem, type LinkedItemType]:             # <<<<<<<<<<<<<<
 *     cdef object val
 *     cdef linkedlist next
 */
struct LinkedItem {
  PyObject_HEAD
  PyObject *val;
  struct LinkedList *next;
};

/* "recordclass/_linkedlist.pyx":8
 *     cdef linkedlist next
 * 
 * cdef public class linkedlist[object LinkedList, type LinkedListType]:             # <<<<<<<<<<<<<<
 *     cdef linkeditem start
 *     cdef linkeditem end
 */
struct LinkedList {
  PyObject_HEAD
  struct __pyx_vtabstruct_11recordclass_11_linkedlist_linkedlist *__pyx_vtab;
  struct LinkedItem *start;
  struct LinkedItem *end;
};

#ifndef __PYX_HAVE_API__recordclass___linkedlist

#ifndef __PYX_EXTERN_C
  #ifdef __cplusplus
    #define __PYX_EXTERN_C extern "C"
  #else
    #define __PYX_EXTERN_C extern
  #endif
#endif

#ifndef DL_IMPORT
  #define DL_IMPORT(_T) _T
#endif

__PYX_EXTERN_C DL_IMPORT(PyTypeObject) LinkedItemType;
__PYX_EXTERN_C DL_IMPORT(PyTypeObject) LinkedListType;

#endif /* !__PYX_HAVE_API__recordclass___linkedlist */

/* WARNING: the interface of the module init function changed in CPython 3.5. */
/* It now returns a PyModuleDef instance instead of a PyModule instance. */

#if PY_MAJOR_VERSION < 3
PyMODINIT_FUNC init_linkedlist(void);
#else
PyMODINIT_FUNC PyInit__linkedlist(void);
#endif

#endif /* !__PYX_HAVE__recordclass___linkedlist */
