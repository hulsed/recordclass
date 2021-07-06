// The MIT License (MIT)

// Copyright (c) «2015-2021» «Shibzukhov Zaur, szport at gmail dot com»

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software - recordclass library - and associated documentation files 
// (the "Software"), to deal in the Software without restriction, including 
// without limitation the rights to use, copy, modify, merge, publish, distribute, 
// sublicense, and/or sell copies of the Software, and to permit persons to whom 
// the Software is furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifdef Py_LIMITED_API
#undef Py_LIMITED_API
#endif

/*#include "pyconfig.h"*/
#include "Python.h"
#include <string.h>

static PyTypeObject PyLiteTuple_Type;
static PyTypeObject PyMLiteTuple_Type;

#define PyLiteTuple_GET_ITEM(op, i) (((PyLiteTupleObject *)(op))->ob_item[i])
#define PyLiteTuple_SET_ITEM(op, i, v) (((PyLiteTupleObject *)(op))->ob_item[i] = v)
#define PyLiteTuple_GET_SIZE(seq) PyTuple_GET_SIZE(seq)

#define PyLiteTuple_CheckExact(op) (Py_TYPE(op) == &PyLiteTuple_Type || Py_TYPE(op) == &PyMLiteTuple_Type)
#define PyLiteTuple_Check(op) (PyLiteTuple_CheckExact(op) || PyObject_IsInstance(op, (PyObject*)&PyLiteTuple_Type) || PyObject_IsInstance(op, (PyObject*)&PyMLiteTuple_Type))

#define DEFERRED_ADDRESS(addr) 0

static PyTypeObject PyLiteTuple_Type;
typedef PyTupleObject PyLiteTupleObject;

static PyObject *
PyLiteTuple_New(PyTypeObject *tp, Py_ssize_t size)
{
    PyLiteTupleObject *op;
    int is_gc;
    
    if (size < 0) {
        PyErr_BadInternalCall();
        return NULL;
    }
    
    is_gc = PyType_IS_GC(tp);
    if (is_gc)
        op = (PyLiteTupleObject*)_PyObject_GC_NewVar(tp, size);
    else
        op = (PyLiteTupleObject*)_PyObject_NewVar(tp, size);
    if (op == NULL)
        return NULL;

    memset(op->ob_item, 0, Py_SIZE(op)*sizeof(void*));
    
    if (is_gc)
        PyObject_GC_Track(op);
    
    return (PyObject*)op;
}

static PyObject *
litetuple_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyTupleObject *tmp; 
    PyLiteTupleObject *newobj;
    Py_ssize_t i, n;
    PyObject *item;

    if (args == NULL)
        return PyLiteTuple_New(type, 0);
    
    if (PyTuple_CheckExact(args)) {
        tmp = (PyTupleObject*)args;
        Py_INCREF(args);
    } else {
        tmp = (PyTupleObject*)PySequence_Tuple(args);
        if (tmp == NULL)
            return NULL;        
    }

    n = PyTuple_GET_SIZE(tmp);

    if (type == &PyLiteTuple_Type) {
        newobj = (PyLiteTupleObject*)PyLiteTuple_New(type, n);
    } else {
        newobj = (PyLiteTupleObject*)(type->tp_alloc(type, n));
        if (type->tp_flags & Py_TPFLAGS_HEAPTYPE)
            Py_INCREF(type);
    }

    if (newobj == NULL) {
        Py_DECREF(tmp);
        return NULL;
    }

    for (i = n; --i >= 0; ) {
        item = PyTuple_GET_ITEM(tmp, i);
        newobj->ob_item[i] = item;
        Py_INCREF(item);
    }

    Py_DECREF(tmp);
    return (PyObject*)newobj;
}

static PyObject *
litetuple_getnewargs(PyLiteTupleObject *ob)
{
    PyObject *v;
    PyTupleObject *res;
    Py_ssize_t i, n = Py_SIZE(ob);

    res = (PyTupleObject*)PyTuple_New(n);

    if (res == NULL)
        return NULL;

    for (i = n; --i >= 0; ) {
        v = PyTuple_GET_ITEM(ob, i);
        PyTuple_SET_ITEM(res, i, v);
        Py_INCREF(v);
    }

    return (PyObject*)res;
}

static int
litetuple_clear(PyLiteTupleObject *op)
{
    Py_ssize_t i;

    for (i = Py_SIZE(op); --i >= 0; ) {
        Py_CLEAR(op->ob_item[i]);
    }
    return 0;
}

static void
litetuple_dealloc(PyLiteTupleObject *op)
{
    Py_ssize_t i;

    if (PyType_IS_GC(Py_TYPE(op))) 
        PyObject_GC_UnTrack(op);
    else
        for (i = Py_SIZE(op); --i >= 0; ) {
            Py_CLEAR(op->ob_item[i]);
        }

    Py_TYPE(op)->tp_free((PyObject *)op);
}

static void litetuple_free(void *o) {
#if PY_VERSION_HEX >= 0x03080000
    PyTypeObject *type = Py_TYPE(o);
#endif

    if (PyType_IS_GC(Py_TYPE((PyObject*)o)))
        PyObject_GC_Del((PyObject*)o);
    else
        PyObject_Del((PyObject*)o);

#if PY_VERSION_HEX >= 0x03080000
    // This was not needed before Python 3.8 (Python issue 35810)
    Py_DECREF(type);
#endif
}

static int
litetuple_traverse(PyLiteTupleObject *o, visitproc visit, void *arg)
{
    Py_ssize_t i;

    for (i = Py_SIZE(o); --i >= 0; ) {
        Py_VISIT(o->ob_item[i]);
    }
    return 0;
}

static PyObject *
litetuple_repr(PyObject *dd)
{
    PyObject *baserepr;
    PyObject *v, *result;
    Py_ssize_t n;
        
    n = PyTuple_GET_SIZE(dd);

    if (n == 0) {
        result = PyUnicode_FromString("litetuple()\0");
        return result;
    }

    if (n == 1) {
        v = PyTuple_GET_ITEM(dd, 0);
        baserepr = PyObject_Repr(v);
        result = PyUnicode_FromFormat("litetuple(%U)", baserepr);
        return result;
    }    
    
    baserepr = PyTuple_Type.tp_repr(dd);
    if (baserepr == NULL)
        return NULL;

    result = PyUnicode_FromFormat("litetuple%U", baserepr);
    Py_DECREF(baserepr);
    return result;
}

PyDoc_STRVAR(litetuple_doc,
"litetuple([...]) --> litetuple\n\n\
");

static PyObject *
litetuple_concat(PyLiteTupleObject *a, PyObject *bb)
{
    Py_ssize_t size;
    Py_ssize_t i, n;
    PyObject **src, **dest;
    PyLiteTupleObject *np;
    
    if (!PyTuple_Check(bb) && !PyLiteTuple_Check(bb)) {
        PyErr_Format(PyExc_TypeError,
             "can only concatenate tuple (not \"%.200s\") to tuple",
                 Py_TYPE(bb)->tp_name);
        return NULL;
    }
#define b ((PyLiteTupleObject *)bb)
    size = Py_SIZE(a) + Py_SIZE(b);
    if (size < 0)
        return PyErr_NoMemory();

    np = (PyLiteTupleObject *) PyLiteTuple_New(Py_TYPE(a), size);
    if (np == NULL) {
        return NULL;
    }
    
    src = a->ob_item;
    dest = np->ob_item;

    n = Py_SIZE(a);
    if (n > 0) {
        for (i = 0; i < n; i++) {
            PyObject *v = src[i];
            Py_INCREF(v);
            dest[i] = v;
        }
    }
    
    src = b->ob_item;
    dest = np->ob_item + Py_SIZE(a);
    n = Py_SIZE(b);
    for (i = 0; i < n; i++) {
        PyObject *v = src[i];
        Py_INCREF(v);
        dest[i] = v;
    }
#undef b

    return (PyObject *)np;
}

static PyObject *
litetuple_slice(PyLiteTupleObject *a, Py_ssize_t ilow, Py_ssize_t ihigh)
{
#define aa ((PyLiteTupleObject*)a)
    PyLiteTupleObject *np;
    PyObject **src, **dest;
    Py_ssize_t i;
    Py_ssize_t len;
    
    if (ilow < 0)
        ilow = 0;
    if (ihigh > Py_SIZE(a))
        ihigh = Py_SIZE(a);
    if (ihigh < ilow)
        ihigh = ilow;
//     if (ilow == 0 && ihigh == Py_SIZE(a) && Py_TYPE(a) == &PyLiteTuple_Type) {
//         Py_INCREF(a);
//         return (PyObject*)a;
//     }

    len = ihigh - ilow;

    np = (PyLiteTupleObject*)(Py_TYPE(a)->tp_alloc(Py_TYPE(a), len));    
    if (np == NULL)
        return NULL;
        
    src = aa->ob_item + ilow;
    dest = np->ob_item;
    if (len > 0) {
        for (i = 0; i < len; i++) {
            PyObject *v = src[i];
            Py_INCREF(v);
            dest[i] = v;
        }
    }
    return (PyObject *)np;
#undef aa
}

// static int
// litetuple_ass_slice(PyLiteTupleObject *a, Py_ssize_t ilow, Py_ssize_t ihigh, PyObject *v)
// {
//     PyObject **item;
//     PyObject **vitem = NULL;
//     PyObject *v_as_SF = NULL; /* PySequence_Fast(v) */
//     Py_ssize_t n;
//     Py_ssize_t k;
//     int result = -1;
    
//     if (v == NULL)
//         return result;
//     else {
//         if ((PyObject*)a == v) {
//             v = litetuple_slice((PyLiteTupleObject*)v, 0, Py_SIZE(v));
//             if (v == NULL)
//                 return result;
                
//             result = litetuple_ass_slice(a, ilow, ihigh, v);
//             Py_DECREF(v);
//             return result;
//         }
//         v_as_SF = PySequence_Fast(v, "can only assign an iterable");
//         if(v_as_SF == NULL) {
//             return result;
//         }
//         n = PySequence_Fast_GET_SIZE(v_as_SF);
//         vitem = PySequence_Fast_ITEMS(v_as_SF);
//     }
    
//     if (ilow < 0)
//         ilow = 0;
//     else if (ilow > Py_SIZE(a))
//         ilow = Py_SIZE(a);

//     if (ihigh < ilow)
//         ihigh = ilow;
//     else if (ihigh > Py_SIZE(a))
//         ihigh = Py_SIZE(a);

//     if (n != ihigh - ilow) {
//         Py_XDECREF(v_as_SF);    
//         return -1;
//     }
    
//     item = ((PyLiteTupleObject*)a)->ob_item;
//     if (n > 0) {
//         for (k = 0; k < n; k++, ilow++) {
//             PyObject *w = vitem[k];
//             PyObject *u = item[ilow];
//             Py_XDECREF(u);
//             item[ilow] = w;
//             Py_XINCREF(w);
//         }
//     }
//     Py_XDECREF(v_as_SF);    
//     return 0;
// }


static int
litetuple_ass_item(PyLiteTupleObject *a, Py_ssize_t i, PyObject *v)
{
    PyObject *old_value;
    Py_ssize_t n = Py_SIZE(a);
    
    if (i < 0)
        i += n;
    if (i < 0 || i >= Py_SIZE(a)) {
        PyErr_SetString(PyExc_IndexError,
                        "assignment index out of range");
        return -1;
    }
    
    if (v == NULL)
        return -1;
        
    old_value = PyLiteTuple_GET_ITEM(a, i);
    Py_XDECREF(old_value);
    PyLiteTuple_SET_ITEM(a, i, v);
    Py_INCREF(v);
    return 0;
}

static PyObject *
litetuple_item(PyLiteTupleObject *a, Py_ssize_t i)
{
    PyObject *v;
    Py_ssize_t n = Py_SIZE(a);
    
    if (i < 0)
        i += n;
    if (i < 0 || i >= n) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }
    v = a->ob_item[i];
    Py_INCREF(v);
    return (PyObject*)v;
}

// static PyObject*
// litetuple_subscript(PyLiteTupleObject* self, PyObject* item)
// {
//     if (PyIndex_Check(item)) {        
//         Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
//         if (i == -1 && PyErr_Occurred())
//             return NULL;
//         return litetuple_item(self, i);
//     }
//     else if (PySlice_Check(item)) {
//         Py_ssize_t start, stop, step, slicelength;

//         if (PySlice_GetIndicesEx(item, (PyTuple_GET_SIZE(self)), &start, &stop, &step, &slicelength) < 0) {
//             return NULL;
//         }
//         return litetuple_slice(self, start, stop);
//     }
//     else {
//         PyErr_Format(PyExc_TypeError,
//                      "subscript must be integer, slice or string, but not %.200s",
//                      Py_TYPE(item)->tp_name);
//         return NULL;
//     }
// }

// static int
// litetuple_ass_subscript(PyLiteTupleObject* self, PyObject* item, PyObject* value)
// {
//     if (PyIndex_Check(item)) {
//         Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
//         if (i == -1 && PyErr_Occurred())
//             return -1;
//         if (i < 0)
//             i += PyList_GET_SIZE(self);
//         return litetuple_ass_item(self, i, value);
//     }
//     else if (PySlice_Check(item)) {
//         Py_ssize_t start, stop, step, slicelength;

//         if (PySlice_GetIndicesEx(item, (Py_SIZE(self)), &start, &stop, &step, &slicelength) < 0) {
//             return -1; 
//         }
//         return litetuple_ass_slice(self, start, stop, value);
//     }
//     else {
//         PyErr_Format(PyExc_TypeError,
//                      "indices must be integers, not %.200s",
//                      Py_TYPE(item)->tp_name);
//         return -1;
//     }
// }

static PyObject *
litetuple_repeat(PyLiteTupleObject *a, Py_ssize_t n)
{
    Py_ssize_t i, j;
    Py_ssize_t size;
    PyTupleObject *np;
    PyObject **p, **items;
    if (n < 0)
        n = 0;
    if (Py_SIZE(a) == 0) {
        return PyLiteTuple_New(Py_TYPE(a), 0);
    }
    if (n > PY_SSIZE_T_MAX / Py_SIZE(a))
        return PyErr_NoMemory();
    size = Py_SIZE(a);
    np = (PyLiteTupleObject *) PyLiteTuple_New(Py_TYPE(a), Py_SIZE(a) * n);
    if (np == NULL)
        return NULL;
    
    if (size == 0)
        return (PyObject *)np;
        
    p = np->ob_item;
    items = a->ob_item;
    for (i = 0; i < n; i++) {
        for (j = 0; j < size; j++) {
            *p = items[j];
            Py_INCREF(*p);
            p++;
        }
    }
    return (PyObject *) np;
}

PyDoc_STRVAR(litetuple_len_doc,
"T.__len__() -- len of T");

static Py_ssize_t
litetuple_len(PyLiteTupleObject *op)
{
    return Py_SIZE(op);
}

PyDoc_STRVAR(litetuple_sizeof_doc,
"T.__sizeof__() -- size of T in memory, in bytes");

static PyObject *
litetuple_sizeof(PyLiteTupleObject *self)
{
    Py_ssize_t res;

    res = PyLiteTuple_Type.tp_basicsize + Py_SIZE(self) * sizeof(PyObject*);
    return PyLong_FromSsize_t(res);
}

static PyObject *
litetuple_richcompare(PyObject *v, PyObject *w, int op)
{
    PyLiteTupleObject *vt, *wt;
    Py_ssize_t i;
    Py_ssize_t vlen, wlen;

    if ((!PyType_IsSubtype(Py_TYPE(v), &PyLiteTuple_Type) || 
         !PyType_IsSubtype(Py_TYPE(v), &PyMLiteTuple_Type)) && 
        (!PyType_IsSubtype(Py_TYPE(w), &PyLiteTuple_Type) || 
         !PyType_IsSubtype(Py_TYPE(w), &PyMLiteTuple_Type)))
        Py_RETURN_NOTIMPLEMENTED;

    vt = (PyLiteTupleObject *)v;
    wt = (PyLiteTupleObject *)w;

    vlen = Py_SIZE(vt);
    wlen = Py_SIZE(wt);
    
    if ((vlen != wlen) && (op == Py_EQ || op == Py_NE)) {
        PyObject *res;
        if (op == Py_EQ)
            res = Py_False;
        else
            res = Py_True;
        Py_INCREF(res);
        return res;
    }    
    
    for (i = 0; i < vlen && i < wlen; i++) {
        int k = PyObject_RichCompareBool(vt->ob_item[i],
                                         wt->ob_item[i], Py_EQ);
        if (k < 0)
            return NULL;
        if (!k)
            break;
    }

    if (i >= vlen || i >= wlen) {
        /* No more items to compare -- compare sizes */
        int cmp;
        PyObject *res;
        switch (op) {
        case Py_LT: cmp = vlen <  wlen; break;
        case Py_LE: cmp = vlen <= wlen; break;
        case Py_EQ: cmp = vlen == wlen; break;
        case Py_NE: cmp = vlen != wlen; break;
        case Py_GT: cmp = vlen >  wlen; break;
        case Py_GE: cmp = vlen >= wlen; break;
        default: return NULL; /* cannot happen */
        }
        if (cmp)
            res = Py_True;
        else
            res = Py_False;
        Py_INCREF(res);
        return res;
    }

    /* We have an item that differs -- shortcuts for EQ/NE */
    if (op == Py_EQ) {
        Py_INCREF(Py_False);
        return Py_False;
    }
    if (op == Py_NE) {
        Py_INCREF(Py_True);
        return Py_True;
    }

    /* Compare the final item again using the proper operator */
    return PyObject_RichCompare(vt->ob_item[i], wt->ob_item[i], op);
}

static PySequenceMethods litetuple_as_sequence = {
    (lenfunc)litetuple_len,                          /* sq_length */
    (binaryfunc)litetuple_concat,                    /* sq_concat */
    (ssizeargfunc)litetuple_repeat,                  /* sq_repeat */
    (ssizeargfunc)litetuple_item,                    /* sq_item */
    0,                                                 /* sq_slice */
    (ssizeobjargproc)litetuple_ass_item,             /* sq_ass_item */
    0,                                                 /* sq_ass_item */
    0,                                                 /* sq_ass_slice */
    0,                                                 /* sq_contains */
};

// static PyMappingMethods litetuple_as_mapping = {
//     (lenfunc)litetuple_len,
//     (binaryfunc)litetuple_subscript,
//     (objobjargproc)litetuple_ass_subscript
// };

static PySequenceMethods litetuple_ro_as_sequence = {
    (lenfunc)litetuple_len,                          /* sq_length */
    (binaryfunc)litetuple_concat,                    /* sq_concat */
    (ssizeargfunc)litetuple_repeat,                  /* sq_repeat */
    (ssizeargfunc)litetuple_item,                    /* sq_item */
    0,                                                 /* sq_slice */
    0,                                                 /* sq_ass_item */
    0,                                                 /* sq_ass_item */
    0,                                                 /* sq_ass_slice */
    0,                                                 /* sq_contains */
};

// static PyMappingMethods litetuplereadonly_as_mapping = {
//     (lenfunc)litetuple_len,
//     (binaryfunc)litetuple_subscript,
//     0
// };

PyDoc_STRVAR(litetuple_copy_doc, "D.copy() -> a shallow copy of D.");

static PyObject *
litetuple_copy(PyLiteTupleObject *ob)
{
    return litetuple_slice(ob, 0, PyTuple_GET_SIZE(ob));
}


PyDoc_STRVAR(litetuple_reduce_doc, "D.__reduce__()");

static PyObject *
litetuple_reduce(PyObject *ob)
{
    PyObject *args;
    PyObject *result;
    PyObject *tmp;

    tmp = PySequence_Tuple(ob);
    args = PyTuple_Pack(1, tmp);
    Py_DECREF(tmp);
    if (args == NULL)
        return NULL;

    result = PyTuple_Pack(2, &PyLiteTuple_Type, args);
    Py_DECREF(args);
    return result;
}

static long
litetuple_hash(PyObject *v)
{
    register long x, y;
    register Py_ssize_t len = Py_SIZE(v);
    register PyObject **p;
    long mult = 1000003L;
    x = 0x345678L;
    p = ((PyTupleObject*)v)->ob_item;
    while (--len >= 0) {
        y = PyObject_Hash(*p++);
        if (y == -1)
            return -1;
        x = (x ^ y) * mult;
        /* the cast might truncate len; that doesn't change hash stability */
        mult += (long)(82520L + len + len);
    }
    x += 97531L;
    if (x == -1)
        x = -2;
    return x;
}

static PyMethodDef litetuple_methods[] = {
    {"__getnewargs__",          (PyCFunction)litetuple_getnewargs,  METH_NOARGS},
        /*{"copy", (PyCFunction)litetuple_copy, METH_NOARGS, litetuple_copy_doc},*/
    {"__copy__", (PyCFunction)litetuple_copy, METH_NOARGS, litetuple_copy_doc},
    {"__len__", (PyCFunction)litetuple_len, METH_NOARGS, litetuple_len_doc},
    {"__sizeof__",      (PyCFunction)litetuple_sizeof, METH_NOARGS, litetuple_sizeof_doc},     
    {"__reduce__", (PyCFunction)litetuple_reduce, METH_NOARGS, litetuple_reduce_doc},
    {NULL}
};

static PyObject* 
litetuple_iter(PyObject *seq);


static PyTypeObject PyLiteTuple_Type = {
    PyVarObject_HEAD_INIT(DEFERRED_ADDRESS(&PyType_Type), 0)
    "recordclass.litetuple.litetuple",          /* tp_name */
    sizeof(PyLiteTupleObject) - sizeof(PyObject*),      /* tp_basicsize */
    sizeof(PyObject*),                              /* tp_itemsize */
    /* methods */
    (destructor)litetuple_dealloc,        /* tp_dealloc */
    0,                                      /* tp_print */
    0,                                      /* tp_getattr */
    0,                                      /* tp_setattr */
    0,                                      /* tp_reserved */
    (reprfunc)litetuple_repr,             /* tp_repr */
    0,                                      /* tp_as_number */
    &litetuple_ro_as_sequence,               /* tp_as_sequence */
    0,                /* tp_as_mapping */
    PyObject_HashNotImplemented,            /* tp_hash */
    0,                                      /* tp_call */
    0,                                      /* tp_str */
    PyObject_GenericGetAttr,                /* tp_getattro */
    PyObject_GenericSetAttr,                /* tp_setattro */
    0,                                      /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
                                            /* tp_flags */
    litetuple_doc,                        /* tp_doc */
    (traverseproc)litetuple_traverse,     /* tp_traverse */
    (inquiry)litetuple_clear,             /* tp_clear */
    litetuple_richcompare,                /* tp_richcompare */
    0,                                      /* tp_weaklistoffset*/
    litetuple_iter,                       /* tp_iter */
    0,                                      /* tp_iternext */
    litetuple_methods,                    /* tp_methods */
    0,                                      /* tp_members */
    0,                                      /* tp_getset */
    0,                                      /* tp_base */
    0,                                      /* tp_litetuple */
    0,                                      /* tp_descr_get */
    0,                                      /* tp_descr_set */
    0,                                      /* tp_litetupleoffset */
    0,                                      /* tp_init */
    0,                                      /* tp_alloc */
    litetuple_new,                        /* tp_new */
    litetuple_free,                        /* tp_free */
    0                                       /* tp_is_gc */
};

static PyTypeObject PyMLiteTuple_Type = {
    PyVarObject_HEAD_INIT(DEFERRED_ADDRESS(&PyType_Type), 0)
    "recordclass.litetuple.mlitetuple",          /* tp_name */
    sizeof(PyLiteTupleObject) - sizeof(PyObject*),      /* tp_basicsize */
    sizeof(PyObject*),                              /* tp_itemsize */
    /* methods */
    (destructor)litetuple_dealloc,        /* tp_dealloc */
    0,                                      /* tp_print */
    0,                                      /* tp_getattr */
    0,                                      /* tp_setattr */
    0,                                      /* tp_reserved */
    (reprfunc)litetuple_repr,             /* tp_repr */
    0,                                      /* tp_as_number */
    &litetuple_as_sequence,               /* tp_as_sequence */
    0,                /* tp_as_mapping */
    litetuple_hash,                       /* tp_hash */
    0,                                      /* tp_call */
    0,                                      /* tp_str */
    PyObject_GenericGetAttr,                /* tp_getattro */
    PyObject_GenericSetAttr,                /* tp_setattro */
    0,                                      /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
                                            /* tp_flags */
    litetuple_doc,                        /* tp_doc */
    (traverseproc)litetuple_traverse,     /* tp_traverse */
    (inquiry)litetuple_clear,             /* tp_clear */
    litetuple_richcompare,                /* tp_richcompare */
    0,                                      /* tp_weaklistoffset*/
    litetuple_iter,                       /* tp_iter */
    0,                                      /* tp_iternext */
    litetuple_methods,                    /* tp_methods */
    0,                                      /* tp_members */
    0,                                      /* tp_getset */
    0,                                      /* tp_base */
    0,                                      /* tp_litetuple */
    0,                                      /* tp_descr_get */
    0,                                      /* tp_descr_set */
    0,                                      /* tp_litetupleoffset */
    0,                                      /* tp_init */
    0,                                      /* tp_alloc */
    litetuple_new,                        /* tp_new */
    litetuple_free,                        /* tp_free */
    0                                       /* tp_is_gc */
};

/*********************** MLiteTuple Iterator **************************/

typedef struct {
    PyObject_HEAD
    Py_ssize_t it_index;
    PyLiteTupleObject *it_seq; /* Set to NULL when iterator is exhausted */
} litetupleiterobject;

static void
litetupleiter_dealloc(litetupleiterobject *it)
{
    PyObject_GC_UnTrack(it);
    Py_CLEAR(it->it_seq);
    PyObject_GC_Del(it);
}

static int
litetupleiter_traverse(litetupleiterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->it_seq);
    return 0;
}

static int
litetupleiter_clear(litetupleiterobject *it)
{
    Py_CLEAR(it->it_seq);
    return 0;
}

static PyObject *
litetupleiter_next(litetupleiterobject *it)
{
    PyTupleObject *seq;
    PyObject *item;

//     assert(it != NULL);
    seq = it->it_seq;
    if (seq == NULL)
        return NULL;
//     assert(PyTuple_Check(seq));

    if (it->it_index < PyLiteTuple_GET_SIZE(seq)) {
        item = PyLiteTuple_GET_ITEM(seq, it->it_index);
        Py_INCREF(item);
        ++it->it_index;
        return item;
    }

    Py_DECREF(seq);
    it->it_seq = NULL;
    return NULL;
}

static PyObject *
litetupleiter_len(litetupleiterobject *it)
{
    Py_ssize_t len = 0;
    if (it->it_seq)
        len = PyLiteTuple_GET_SIZE(it->it_seq) - it->it_index;
    return PyLong_FromSsize_t(len);
}

PyDoc_STRVAR(length_hint_doc, "Private method returning an estimate of len(list(it)).");

static PyObject *
litetupleiter_reduce(litetupleiterobject *it) //, PyObject *Py_UNUSED(ignore))
{
#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 8
    _Py_IDENTIFIER(iter);
    if (it->it_seq)
        return Py_BuildValue("N(O)n", _PyEval_GetBuiltinId(&PyId_iter),
                             it->it_seq, it->it_index);
    else
        return Py_BuildValue("N(())", _PyEval_GetBuiltinId(&PyId_iter));
#else
    if (it->it_seq)
        return Py_BuildValue("N(O)n", _PyObject_GetBuiltin("iter"),
                             it->it_seq, it->it_index);
    else
        return Py_BuildValue("N(())", _PyObject_GetBuiltin("iter"));
#endif
}

PyDoc_STRVAR(litetupleiter_reduce_doc, "D.__reduce__()");


static PyObject *
litetupleiter_setstate(litetupleiterobject *it, PyObject *state)
{
    Py_ssize_t index;

    index = PyLong_AsSsize_t(state);
    if (index == -1 && PyErr_Occurred())
        return NULL;
    if (it->it_seq != NULL) {
        if (index < 0)
            index = 0;
        else if (index > PyLiteTuple_GET_SIZE(it->it_seq))
            index = PyLiteTuple_GET_SIZE(it->it_seq); /* exhausted iterator */
        it->it_index = index;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(setstate_doc, "Set state information for unpickling.");


static PyMethodDef litetupleiter_methods[] = {
    {"__length_hint__", (PyCFunction)litetupleiter_len, METH_NOARGS, length_hint_doc},
    {"__reduce__",      (PyCFunction)litetupleiter_reduce, METH_NOARGS, litetupleiter_reduce_doc},
    {"__setstate__",    (PyCFunction)litetupleiter_setstate, METH_O, setstate_doc},
    {NULL,              NULL}           /* sentinel */
};

PyTypeObject PyLiteTupleIter_Type = {
    PyVarObject_HEAD_INIT(DEFERRED_ADDRESS(&PyType_Type), 0)
    "recordclass.litetuple.litetuple_iterator",                           /* tp_name */
    sizeof(litetupleiterobject),                    /* tp_basicsize */
    0,                                          /* tp_itemsize */
    /* methods */
    (destructor)litetupleiter_dealloc,              /* tp_dealloc */
    0,                                          /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_reserved */
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,    /* tp_flags */
    0,                                          /* tp_doc */
    (traverseproc)litetupleiter_traverse,     /* tp_traverse */
    (inquiry)litetupleiter_clear,             /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    PyObject_SelfIter,                          /* tp_iter */
    (iternextfunc)litetupleiter_next,         /* tp_iternext */
    litetupleiter_methods,                    /* tp_methods */
    0,
};

static PyObject *
litetuple_iter(PyObject *seq)
{
    litetupleiterobject *it;

    it = PyObject_GC_New(litetupleiterobject, &PyLiteTupleIter_Type);
    if (it == NULL)
        return NULL;
    it->it_index = 0;
    it->it_seq = (PyTupleObject *)seq;
    Py_INCREF(seq);
    PyObject_GC_Track(it);
    return (PyObject *)it;
}


/* List of functions defined in the module */

PyDoc_STRVAR(litetuplemodule_doc,
"Litetuple module provide mutable and immutable tuple types without cyclic garbage collection (reference count only).");

// static PyObject *
// litetuple_freeze(PyObject *module, PyObject *args)
// {
//     if (Py_SIZE(args) > 2) 
//         return NULL;
//     PyObject *ob = PyTuple_GET_ITEM(args, 0);
//     PyObject *is_copy = PyTuple_GET_ITEM(args, 1);
    
//     if (PyObject_IsInstance(&PyLiteTupleTypeRO_Type) {
//         Py_INCREF(ob);
//         return ob;
//     }
        
//     if (PyObject_Bool(is_copy))
//         ob = litetuple_slice(ob, 0, Py_SIZE(ob));
    
//     if (PyObject_IsInstance(&PyLiteTupleType_Type) {
//         Py_DECREF((PyObject*)Py_TYPE(ob));
//         Py_TYPE(ob) = &PyLiteTupleType_Type;
//         Py_INCREF((PyObject*)&PyLiteTupleType_Type);
//     }
    
//     return litetuple_slice(ob, 0, PyTuple_GET_SIZE(ob));
// }

static PyMethodDef litetuplemodule_methods[] = {
//   {"getitem", get_item,     METH_VARARGS,   "__getitem__"},
//   {"freeze", litetuple_freeze,     METH_VARARGS,   "freeze litetuple object (make it readonly and hashable)"},
   {0, 0, 0, 0}
};

static struct PyModuleDef litetuplemodule = {
    PyModuleDef_HEAD_INIT,
    "recordclass.litetuple",
    litetuplemodule_doc,
    -1,
    litetuplemodule_methods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit_litetuple(void)
{
    PyObject *m;
    
    m = PyState_FindModule(&litetuplemodule);
    if (m) {
        Py_INCREF(m);
        return m;
    }    

    m = PyModule_Create(&litetuplemodule);
    if (m == NULL)
        return NULL;

    if (PyType_Ready(&PyLiteTuple_Type) < 0)
        Py_FatalError("Can't initialize litetuple type");

    if (PyType_Ready(&PyMLiteTuple_Type) < 0)
         Py_FatalError("Can't initialize litetuplereadonly type");

    if (PyType_Ready(&PyLiteTupleIter_Type) < 0)
        Py_FatalError("Can't initialize litetuple iter type");
    
#if PY_VERSION_HEX >= 0x03080000
    if (PyLiteTuple_Type.tp_flags & Py_TPFLAGS_METHOD_DESCRIPTOR)
        PyLiteTuple_Type.tp_flags &= ~Py_TPFLAGS_METHOD_DESCRIPTOR;
    if (PyMLiteTuple_Type.tp_flags & Py_TPFLAGS_METHOD_DESCRIPTOR)
        PyMLiteTuple_Type.tp_flags &= ~Py_TPFLAGS_METHOD_DESCRIPTOR;
    if (PyLiteTupleIter_Type.tp_flags & Py_TPFLAGS_METHOD_DESCRIPTOR)
        PyLiteTupleIter_Type.tp_flags &= ~Py_TPFLAGS_METHOD_DESCRIPTOR;
#endif
    
    Py_INCREF(&PyLiteTuple_Type);
    PyModule_AddObject(m, "litetuple", (PyObject *)&PyLiteTuple_Type);

    Py_INCREF(&PyMLiteTuple_Type);
    PyModule_AddObject(m, "mlitetuple", (PyObject *)&PyMLiteTuple_Type);

//     Py_INCREF(&PyLiteTupleIter_Type);    
//     PyModule_AddObject(m, "litetupleiter", (PyObject *)&PyLiteTupleIter_Type);
    

    return m;
}