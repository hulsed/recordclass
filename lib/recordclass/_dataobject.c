// The MIT License (MIT)

// Copyright (c) «2015-2019» «Shibzukhov Zaur, szport at gmail dot com»

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

#include "pyconfig.h"
#include "Python.h"
#include <string.h>

#include "_dataobject.h"

#define DEFERRED_ADDRESS(addr) 0

#if PY_MAJOR_VERSION > 2
#define IsStr(op) PyUnicode_CheckExact(op)
#else
#define IsStr(op) (PyString_CheckExact(op) || PyUnicode_CheckExact(op))
#endif

#ifndef Py_RETURN_NOTIMPLEMENTED
#define Py_RETURN_NOTIMPLEMENTED \
    return Py_INCREF(Py_NotImplemented), Py_NotImplemented
#endif

#if PY_MAJOR_VERSION == 2
static PyObject *
_PyObject_GetBuiltin(const char *name)
{
    PyObject *mod_name, *mod, *attr;

    mod_name = PyUnicode_FromString("__builtin__");   /* borrowed */
    if (mod_name == NULL)
        return NULL;
    mod = PyImport_Import(mod_name);
    if (mod == NULL)
        return NULL;
    attr = PyObject_GetAttrString(mod, name);
    Py_DECREF(mod);
    return attr;
}
#endif

static PyObject *
_PyObject_GetObject(const char *modname, const char *name)
{
    PyObject *mod_name, *mod, *mod_dict, *attr;

    mod_name = PyUnicode_FromString(modname);   /* borrowed */
    if (mod_name == NULL)
        return NULL;
    mod = PyImport_Import(mod_name);
    if (mod == NULL)
        return NULL;
    mod_dict = PyObject_GetAttrString(mod, "__dict__");
    if (mod_dict == NULL)
        return NULL;
    attr = PyDict_GetItemString(mod_dict, name);
    Py_DECREF(mod);
    Py_DECREF(mod_dict);
    return attr;
}

// forward decaration
static Py_ssize_t do_getlen(PyObject *op);
static PyObject* do_getitem(PyObject *op, Py_ssize_t i);
static PyObject* _astuple(PyObject *op);

static PyObject *
dataobject_alloc(PyTypeObject *type, Py_ssize_t n_items)
{
    PyObject *op;
    Py_ssize_t size = _PyObject_SIZE(type);
    int is_gc = type->tp_flags & Py_TPFLAGS_HAVE_GC;

    if (is_gc)
        op = _PyObject_GC_Malloc(size);
    else
        op = (PyObject *)PyObject_MALLOC(size);

    if (op == NULL)
        return PyErr_NoMemory();

    memset(op, '\0', size);

    Py_TYPE(op) = type;
    if (type->tp_flags & Py_TPFLAGS_HEAPTYPE)
        Py_INCREF(type);

    _Py_NewReference(op);
    
    if (is_gc)
        PyObject_GC_Track(op);
    
    return op;
}

static PyObject*
dataobject_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *op;
    Py_ssize_t n_slots;
    PyObject *tmp;
    Py_ssize_t n_args;
    PyObject **items, **pp;
    PyObject *v;

    if (Py_TYPE(args) == &PyTuple_Type) {
        tmp = args;
        Py_INCREF(args);
    } else {
        tmp = PySequence_Tuple(args);
        if (tmp == NULL) {
            return NULL;
        }
    }
    
    n_args = PyTuple_GET_SIZE(tmp);

    n_slots = dataobject_numslots(type);
    if (n_args > n_slots) {
        PyErr_SetString(PyExc_TypeError,
                        "number of the arguments should not be greater than the number of the slots");
        Py_DECREF(tmp);
        return NULL;            
    }

    op = type->tp_alloc(type, 0);

    items = dataobject_slots(op);
    pp = ((PyTupleObject*)tmp)->ob_item;
    while (n_args--) {
        v = *(pp++);
        Py_INCREF(v);
        *(items++) = v;
        n_slots--;
    }

    while (n_slots--) {
        Py_INCREF(Py_None);
        *(items++) = Py_None;
    }
    
    Py_DECREF(tmp);
    
    if (kwds) {
        if (type->tp_dictoffset) {
            PyObject **dictptr = dataobject_dictptr(type, op);
            PyObject *dict;
            if (*dictptr)
                dict = *dictptr;
            else {
                dict = PyDict_New();
                *dictptr = dict;
            }

            if (PyDict_Update(dict, kwds) == -1) {
                PyErr_SetString(PyExc_TypeError, "__dict__ update is failed");
                return NULL;            
            }
        } else {
            PyErr_SetString(PyExc_TypeError, "instance hasn't __dict__");
            return NULL;            
        }        
    }

    return op;
}

static int
dataobject_clear(PyObject *op)
{
    PyTypeObject *type = Py_TYPE(op);
    PyObject **items = dataobject_slots(op);
    Py_ssize_t n_slots = dataobject_numslots(type);
    PyObject *v;

    if (type->tp_weaklistoffset)
        PyObject_ClearWeakRefs(op);

    if (type->tp_dictoffset) {
        PyObject **dictptr = dataobject_dictptr(type, op);
        if (dictptr && *dictptr)
            Py_CLEAR(*dictptr);
    }

    while (n_slots-- > 0) {
        v = *(items++);
        Py_CLEAR(v);
    }

    return 0;
}

static void
dataobject_dealloc(PyObject *op)
{
    PyTypeObject *type = Py_TYPE(op); 
    PyObject **items = dataobject_slots(op);
    Py_ssize_t n_slots = dataobject_numslots(type);
    PyObject *v;

    if (PyType_IS_GC(type))
        PyObject_GC_UnTrack(op);

    if (type->tp_weaklistoffset)
        PyObject_ClearWeakRefs(op);

    if (type->tp_dictoffset) {
        PyObject **dictptr = dataobject_dictptr(type, op);
        if (dictptr && *dictptr)
            Py_CLEAR(*dictptr);
    }
    
    while (n_slots-- > 0) {
        v = *(items++);
        Py_CLEAR(v);
    }    

    type->tp_free((PyObject *)op);
}

static void
dataobject_free(void *op)
{
    PyTypeObject *type = Py_TYPE((PyObject*)op);
    int is_gc = PyType_IS_GC(type);
    
    if (!is_gc)
        PyObject_Del((PyObject*)op);
    else
        PyObject_GC_Del((PyObject*)op);

    if (type->tp_flags & Py_TPFLAGS_HEAPTYPE)
        Py_DECREF(type);
}

static int
dataobject_traverse(PyObject *op, visitproc visit, void *arg)
{
    Py_ssize_t n_slots;
    PyObject **items;
    PyTypeObject *type = Py_TYPE(op);
    PyObject *v;

    n_slots = dataobject_numslots(type);

    if (n_slots) {
        items = dataobject_slots(op);
        while (n_slots--) {
            v = *(items++);
            Py_VISIT(v);
        }
    }

    if (type->tp_dictoffset) {
        PyObject **dictptr = dataobject_dictptr(type, op);
        if (dictptr && *dictptr)
            Py_VISIT(*dictptr);
    }

    return 0;
}

static PyObject *
dataobject_item(PyObject *op, Py_ssize_t i)
{
    PyObject **items;
    PyObject *v;
    
    Py_ssize_t n = dataobject_numslots(Py_TYPE(op));

    if (i < 0)
        i += n;
    if (i < 0 || i >= n) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }

    items = dataobject_slots(op);
    v = items[i];
    Py_INCREF(v);
    return v;
}

static int
dataobject_ass_item(PyObject *op, Py_ssize_t i, PyObject *val)
{
    PyObject **items;
    PyObject* old_val;
        
    Py_ssize_t n = dataobject_numslots(Py_TYPE(op));

    if (i < 0)
        i += n;
    if (i < 0 || i >= n) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return -1;
    }

    items = dataobject_slots(op);

    items += i;

    old_val = *items;
    if (old_val)
        Py_DECREF(old_val);
    Py_INCREF(val);
    *items = val;
    return 0;
}

static PyObject*
dataobject_subscript(PyObject* op, PyObject* item)
{
    return PyObject_GetAttr(op, item);
}

static int
dataobject_ass_subscript(PyObject* op, PyObject* item, PyObject *val)
{
    return PyObject_SetAttr(op, item, val);
}

static int
dataobject_ass_subscript2(PyObject* op, PyObject* item, PyObject *val)
{
    if (PyIndex_Check(item)) {
        Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return -1;
        return dataobject_ass_item(op, i, val);
    } else
        return PyObject_SetAttr(op, item, val);
}

static PyObject*
dataobject_subscript2(PyObject* op, PyObject* item)
{
    if (PyIndex_Check(item)) {
        Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return NULL;
        return dataobject_item(op, i);
    } else
        return PyObject_GetAttr(op, item);
}

#ifndef _PyHASH_MULTIPLIER
#define _PyHASH_MULTIPLIER 1000003UL
#endif

static long
dataobject_hash(PyObject *op)
{
    unsigned long x;
    long y;
    Py_ssize_t i, len = do_getlen(op);
    long mult = _PyHASH_MULTIPLIER;
    PyObject *o;

    x = 0x345678L;
    for(i=0; i<len; i++) {
        o = do_getitem(op, i);
//         Py_INCREF(o);
        y = PyObject_Hash(o);
        Py_DECREF(o);
        if (y == -1)
            return -1;
        x = (x ^ y) * mult;
        mult += (long)(82520L + len + len);
    }

    x += 97531L;
    if (x == (unsigned long)-1)
        x = -2;
    return x;
}

static PyObject *
dataobject_richcompare(PyObject *v, PyObject *w, int op)
{
    Py_ssize_t i, k;
    Py_ssize_t vlen, wlen;
    PyObject *vv;
    PyObject *ww;
    PyObject *ret;

    if (!(Py_TYPE(v) == Py_TYPE(w)) || (!PyObject_IsSubclass((PyObject*)Py_TYPE(w), (PyObject*)Py_TYPE(v))))
        Py_RETURN_NOTIMPLEMENTED;

    vlen = do_getlen(v);
    wlen = do_getlen(w);

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
        vv = do_getitem(v, i);
        ww = do_getitem(w, i);
//         Py_INCREF(vv);
//         Py_INCREF(ww);
        k = PyObject_RichCompareBool(vv, ww, Py_EQ);
        Py_DECREF(vv);
        Py_DECREF(ww);
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
    vv = do_getitem(v, i);
    ww = do_getitem(w, i); 
//     Py_INCREF(vv);
//     Py_INCREF(ww);    
    ret = PyObject_RichCompare(vv, ww, op);
    Py_DECREF(vv);
    Py_DECREF(ww);

    return ret;
}

PyDoc_STRVAR(dataobject_len_doc,
"T.__len__() -- len of T");

static Py_ssize_t
dataobject_len(PyObject *op)
{
    PyTypeObject *type = Py_TYPE(op);
    Py_ssize_t n;

    n = dataobject_numslots(type);
    if (type->tp_itemsize)
        n += Py_SIZE(op);

    return n;
}

static PySequenceMethods dataobject_as_sequence0 = {
    (lenfunc)dataobject_len,                          /* sq_length */
    0,                                              /* sq_concat */
    0,                                              /* sq_repeat */
    0,                    /* sq_item */
    0,                                              /* sq_slice */
    0,             /* sq_ass_item */
    0,                                              /* sq_ass_slice */
    0,                                              /* sq_contains */
};

static PySequenceMethods dataobject_as_sequence = {
    (lenfunc)dataobject_len,                          /* sq_length */
    0,                                              /* sq_concat */
    0,                                              /* sq_repeat */
    (ssizeargfunc)dataobject_item,                    /* sq_item */
    0,                                              /* sq_slice */
    (ssizeobjargproc)dataobject_ass_item,             /* sq_ass_item */
    0,                                              /* sq_ass_slice */
    0,                                              /* sq_contains */
};

static PyMappingMethods dataobject_as_mapping0 = {
    (lenfunc)dataobject_len,                          /* mp_len */
    0,                 /* mp_subscr */
    0,          /* mp_ass_subscr */
};

static PyMappingMethods dataobject_as_mapping = {
    (lenfunc)dataobject_len,                          /* mp_len */
    (binaryfunc)dataobject_subscript,                 /* mp_subscr */
    (objobjargproc)dataobject_ass_subscript,          /* mp_ass_subscr */
};

static PyMappingMethods dataobject_as_mapping2 = {
    (lenfunc)dataobject_len,                          /* mp_len */
    (binaryfunc)dataobject_subscript2,                 /* mp_subscr */
    (objobjargproc)dataobject_ass_subscript2,          /* mp_ass_subscr */
};


PyDoc_STRVAR(dataobject_sizeof_doc,
"T.__sizeof__() -- size of T");

static PyObject *
dataobject_sizeof(PyObject *self)
{
    PyTypeObject *tp;
    Py_ssize_t res;

    tp = Py_TYPE(self);
    res = tp->tp_basicsize;
    if (tp->tp_itemsize)
        res += Py_SIZE(self) * sizeof(PyObject*);

    return PyLong_FromSsize_t(res);
}

PyDoc_STRVAR(dataobject_copy_doc,
"T.__copy__() -- copy of T");

static PyObject *
dataobject_copy(PyObject* op)
{
    PyTypeObject *type = Py_TYPE(op);
    PyObject *new_op = type->tp_alloc(type, 0);

    Py_ssize_t i, n = dataobject_len(op);
    
    for(i=0; i<n; i++) {
        PyObject *v;

        v = dataobject_item(op, i);
        if (!v) {
            Py_DECREF(new_op);
            return NULL;
        }
        if (dataobject_ass_item(new_op, i, v) < 0) {
            Py_DECREF(new_op);
            Py_DECREF(v);
            return NULL;            
        }
        Py_DECREF(v);
    }

    if (type->tp_dictoffset) {
        PyObject **dictptr = dataobject_dictptr(type, op);
        PyObject *dict = *dictptr;
        
        PyObject **new_dictptr = dataobject_dictptr(type, new_op);
        PyObject *new_dict = *new_dictptr;
        
        if (dict && !new_dict) {
            new_dict = PyDict_New();
            if (!new_dict) {
                PyErr_SetString(PyExc_TypeError, "failed to create new dict");
                return NULL;                                    
            }            
            *new_dictptr = new_dict;            
        }
        
        if (dict) {
            if (PyDict_Update(new_dict, dict) < 0) {
                PyErr_SetString(PyExc_TypeError, "dict update failed");
                return NULL;                        
            }
        }        
    }

    return new_op;
}

#if PY_MAJOR_VERSION == 2

static PyObject *
dataobject_repr(PyObject *self)
{
    Py_ssize_t i, n, n_fs = 0;
    PyObject *fs;
    PyTypeObject *tp = Py_TYPE(self);
    PyObject *tp_name = PyObject_GetAttrString((PyObject*)tp, "__name__");
    PyObject *text, *t;
    PyObject *lc = PyUnicode_FromString("(");
    PyObject *rc = PyUnicode_FromString(")");
    PyObject *cc = PyUnicode_FromString(", ");
    PyObject *eq = PyUnicode_FromString("=");

    n = do_getlen(self);
    if (n == 0) {
        PyObject *s = PyUnicode_FromString("()");
        text = PyUnicode_Concat(tp_name, s);
        Py_DECREF(tp_name);
        Py_DECREF(s);

        Py_DECREF(lc);
        Py_DECREF(rc);
        Py_DECREF(cc);
        Py_DECREF(eq);

        return text;
    }

    i = Py_ReprEnter((PyObject *)self);
    if (i != 0) {
        Py_DECREF(tp_name);
        Py_DECREF(lc);
        Py_DECREF(rc);
        Py_DECREF(cc);
        Py_DECREF(eq);

        return i > 0 ? PyUnicode_FromString("(...)") : NULL;
    }

    text = PyUnicode_Concat(tp_name, lc);
    Py_DECREF(tp_name);

    fs = PyObject_GetAttrString(self, "__fields__");
    if (fs) {
        if (Py_TYPE(fs) == &PyTuple_Type) {
            n_fs = PyObject_Length(fs);
        } else {
            n_fs = (Py_ssize_t)PyNumber_AsSsize_t(fs, PyExc_IndexError);
            if (n_fs < 0) {
                Py_DECREF(fs);
                Py_DECREF(tp_name);
                Py_DECREF(lc);
                Py_DECREF(rc);
                Py_DECREF(cc);
                Py_DECREF(eq);
                return NULL;
            }
            n_fs = 0;
        }
    } else
        PyErr_Clear();

    /* Do repr() on each element. */
    for (i = 0; i < n; ++i) {
        PyObject *s, *ob;
        PyObject *fn;

        if (n_fs > 0 && i < n_fs) {
            fn = PyTuple_GET_ITEM(fs, i);

            t = text;
            text = PyUnicode_Concat(t, fn);
            Py_DECREF(t);

            t = text;
            text = PyUnicode_Concat(t, eq);
            Py_DECREF(t);
        }

        ob = do_getitem(self, i);
        if (ob == NULL)
            goto error;

        s = PyObject_Repr(ob);
        if (s == NULL) {
            Py_DECREF(ob);
            goto error;
        }

        t = text;
        text = PyUnicode_Concat(t, s);
        Py_DECREF(t);
        Py_DECREF(s);

        Py_DECREF(ob);

        if (i < n-1) {
            t = text;
            text = PyUnicode_Concat(t, cc);
            Py_DECREF(t);
        }
    }

    if (tp->tp_dictoffset) {
        PyObject *dict = PyObject_GetAttrString(self, "__dict__");
        PyObject *s;

        if (dict) {            
            if (PyObject_IsTrue(dict)) {
                PyObject *aa = PyUnicode_FromString(", **");

                t = text;
                text = PyUnicode_Concat(t, aa);
                Py_DECREF(t);

                s = PyObject_Repr(dict);
                t = text;
                text = PyUnicode_Concat(t, s);
                Py_DECREF(t);
                Py_DECREF(s);
            }
            Py_DECREF(dict);
        }
    }        
    
    t = text;
    text = PyUnicode_Concat(text, rc);
    Py_DECREF(t);

    Py_ReprLeave((PyObject *)self);

    Py_XDECREF(fs);
    
    Py_DECREF(lc);
    Py_DECREF(rc);
    Py_DECREF(cc);
    Py_DECREF(eq);

    return text;

error:
    Py_ReprLeave((PyObject *)self);

    Py_XDECREF(fs);

    Py_DECREF(lc);
    Py_DECREF(rc);
    Py_DECREF(cc);
    Py_DECREF(eq);

    return NULL;
}

#else

static PyObject *
dataobject_repr(PyObject *self)
{
    Py_ssize_t i, n, n_fs = 0;
    _PyUnicodeWriter writer;
    PyObject *fs;
    PyTypeObject *tp = Py_TYPE(self);
    PyObject *tp_name = PyObject_GetAttrString((PyObject*)tp, "__name__");
    PyObject *text;

    fs = PyObject_GetAttrString(self, "__fields__");
    if (fs) {
        if (Py_TYPE(fs) == &PyTuple_Type) {
            n_fs = PyObject_Length(fs);
        } else {
            n_fs = (Py_ssize_t)PyNumber_AsSsize_t(fs, PyExc_IndexError);
            if (n_fs < 0) {
                Py_DECREF(fs);
                Py_DECREF(tp_name);
                return NULL;
            }
            n_fs = 0;
        }
    } else
        PyErr_Clear();

    n = do_getlen(self);
    if (n == 0) {
        PyObject *s = PyUnicode_FromString("()");
        text = PyUnicode_Concat(tp_name, s);
        Py_DECREF(s);
        Py_DECREF(tp_name);
        return text;
    }

    i = Py_ReprEnter((PyObject *)self);
    if (i != 0) {
        Py_DECREF(tp_name);
        return i > 0 ? PyUnicode_FromString("(...)") : NULL;
    }

    _PyUnicodeWriter_Init(&writer);
    writer.overallocate = 1;
    if (n > 1) {
        /* "(" + "1" + ", 2" * (len - 1) + ")" */
        writer.min_length = 1 + 1 + (2 + 1) * (n-1) + 1;
    }
    else {
        /* "(1,)" */
        writer.min_length = 4;
    }

    if (_PyUnicodeWriter_WriteStr(&writer, tp_name) < 0)
        goto error;

    Py_DECREF(tp_name);

    if (_PyUnicodeWriter_WriteChar(&writer, '(') < 0)
        goto error;

    /* Do repr() on each element. */
    for (i = 0; i < n; ++i) {
        PyObject *s, *ob;
        PyObject *fn;

        if (n_fs > 0 && i < n_fs) {
            fn = PyTuple_GET_ITEM(fs, i);
            Py_INCREF(fn);
            if (_PyUnicodeWriter_WriteStr(&writer, fn) < 0) {
                Py_DECREF(fn);
                goto error;
            }
            Py_DECREF(fn);
            if (_PyUnicodeWriter_WriteChar(&writer, '=') < 0)
                goto error;
        }

        ob = do_getitem(self, i);
        if (ob == NULL)
            goto error;

        s = PyObject_Repr(ob);
        if (s == NULL) {
            Py_DECREF(ob);
            goto error;
        }

        if (_PyUnicodeWriter_WriteStr(&writer, s) < 0) {
            Py_DECREF(s);
            Py_DECREF(ob);
            goto error;
        }
        Py_DECREF(s);
        Py_DECREF(ob);

        if (i < n-1) {
            if (_PyUnicodeWriter_WriteASCIIString(&writer, ", ", 2) < 0)
                goto error;
        }
    }

    Py_XDECREF(fs);
    
    if (tp->tp_dictoffset) {
        PyObject *dict = PyObject_GetAttrString(self, "__dict__");
        PyObject *s;

        if (dict) {            
            if (PyObject_IsTrue(dict)) {
                if (_PyUnicodeWriter_WriteASCIIString(&writer, ", **", 4) < 0)
                    goto error;
                s = PyObject_Repr(dict);
                if (_PyUnicodeWriter_WriteStr(&writer, s) < 0) {
                    Py_DECREF(s);
                    Py_DECREF(dict);
                    goto error;
                }
                Py_DECREF(s);
            }
            Py_DECREF(dict);
        }
    }        
    
    writer.overallocate = 0;

    if (_PyUnicodeWriter_WriteChar(&writer, ')') < 0)
        goto error;

    Py_ReprLeave((PyObject *)self);
    return _PyUnicodeWriter_Finish(&writer);

error:
    Py_XDECREF(fs);

    _PyUnicodeWriter_Dealloc(&writer);
    Py_ReprLeave((PyObject *)self);
    return NULL;
}
#endif

PyDoc_STRVAR(dataobject_reduce_doc,
"T.__reduce__()");

static PyObject *
dataobject_reduce(PyObject *ob, PyObject *Py_UNUSED(ignore))
{
    PyObject *args;
    PyObject *result;
    PyTypeObject *tp = Py_TYPE(ob);
    PyObject *kw = NULL;
    PyObject **dictptr;

    args = _astuple(ob);
    if (args == NULL)
        return NULL;

    if (tp->tp_dictoffset) {
        dictptr = dataobject_dictptr(tp, ob);
        if (dictptr) {
            kw = *dictptr;
            if (kw) Py_INCREF(kw);
        }
    }
    if (kw) {
        result = PyTuple_Pack(3, tp, args, kw);
    } else
        result = PyTuple_Pack(2, tp, args);

    return result;
}

PyDoc_STRVAR(dataobject_getnewargs_doc,
"T.__getnewargs__()");

static PyObject *
dataobject_getnewargs(PyObject *ob)
{
    PyObject *args;

    args = _astuple(ob);
    if (args == NULL)
        return NULL;

    return args;
}

PyDoc_STRVAR(dataobject_getstate_doc,
"T.__getstate__()");

static PyObject *
dataobject_getstate(PyObject *ob) {
    PyTypeObject *tp = Py_TYPE(ob);
    PyObject *kw = NULL;
    PyObject **dictptr;

    if (tp->tp_dictoffset) {
        dictptr = dataobject_dictptr(tp, ob);
        if (dictptr) {
            kw = *dictptr;
            if (kw) {
                Py_INCREF(kw);
                return kw;
            }
        }
    }

    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(dataobject_setstate_doc,
"T.__setstate__()");

static PyObject*
dataobject_setstate(PyObject *ob, PyObject *state) {
    PyTypeObject *tp = Py_TYPE(ob);
    PyObject **dictptr;
    PyObject *dict;

    if (!state || state == Py_None)
        return 0;

    if (tp->tp_dictoffset) {
        dictptr = dataobject_dictptr(tp, ob);
        dict = *dictptr;

        if (!dict) {
            dict = PyDict_New();
            if (!dict) {
                PyErr_SetString(PyExc_TypeError, "failed to create new dict");
                return NULL;                                    
            }
            *dictptr = dict;
        }        
        if (PyDict_Update(dict, state) < 0) {
            PyErr_SetString(PyExc_TypeError, "dict update failed");
            Py_DECREF(dict);
            return NULL;                        
        }
    } else {
        PyErr_SetString(PyExc_TypeError, "object has no __dict__");
        return NULL;                                            
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef dataobject_methods[] = {
    {"__copy__", (PyCFunction)dataobject_copy, METH_NOARGS, dataobject_copy_doc},
    {"__len__", (PyCFunction)dataobject_len, METH_NOARGS, dataobject_len_doc},
    {"__sizeof__",      (PyCFunction)dataobject_sizeof, METH_NOARGS, dataobject_sizeof_doc},
    {"__reduce__",      (PyCFunction)dataobject_reduce, METH_NOARGS, dataobject_reduce_doc},
    {"__getstate__",      (PyCFunction)dataobject_getstate, METH_NOARGS, dataobject_getstate_doc},
    {"__getnewargs__",      (PyCFunction)dataobject_getnewargs, METH_NOARGS, dataobject_getnewargs_doc},
    {"__setstate__",      (PyCFunction)dataobject_setstate, METH_O, dataobject_setstate_doc},
    {NULL}
};

static PyObject *dataobject_iter(PyObject *seq);


PyDoc_STRVAR(dataobject_doc,
"dataobject(...) --> dataobject\n\n\
");

static PyTypeObject PyDataObject_Type = {
    PyVarObject_HEAD_INIT(DEFERRED_ADDRESS(&PyType_Type), 0)
    "recordclass._dataobject.dataobject",        /* tp_name */
    sizeof(PyObject),                       /* tp_basicsize */
    0,                                      /* tp_itemsize */
    /* methods */
    (destructor)dataobject_dealloc,           /* tp_dealloc */
    0,                                      /* tp_print */
    0,                                      /* tp_getattr */
    0,                                      /* tp_setattr */
    0,                                      /* tp_reserved */
    dataobject_repr,                           /* tp_repr */
    0,                                      /* tp_as_number */
    0,                  /* tp_as_sequence */
    &dataobject_as_mapping0,                   /* tp_as_mapping */
    dataobject_hash,                          /* tp_hash */
    0,                                      /* tp_call */
    0,                                      /* tp_str */
    PyObject_GenericGetAttr,                /* tp_getattro */
    PyObject_GenericSetAttr,                /* tp_setattro */
    0,                                      /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_VERSION_TAG|Py_TPFLAGS_BASETYPE,
                                            /* tp_flags */
    dataobject_doc,                           /* tp_doc */
    0,                      /* tp_traverse */
    0,                         /* tp_clear */
    dataobject_richcompare,                   /* tp_richcompare */
    0,                                      /* tp_weaklistoffset*/
    dataobject_iter,                                      /* tp_iter */
    0,                                      /* tp_iternext */
    dataobject_methods,                       /* tp_methods */
    0,                                      /* tp_members */
    0,                                      /* tp_getset */
    0,                                      /* tp_base */
    0,                                      /* tp_dict */
    0,                                      /* tp_descr_get */
    0,                                      /* tp_descr_set */
    0,                                      /* tp_dictoffset */
    0,                                      /* tp_init */
    dataobject_alloc,                         /* tp_alloc */
    dataobject_new,                   /* tp_new */
    dataobject_free,                          /* tp_free */
    0                                       /* tp_is_gc */
};

/////////////// datatuple /////////////////////////////////

static PyObject *
datatuple_alloc(PyTypeObject *type, Py_ssize_t n_items)
{
    PyObject *op;
    Py_ssize_t size = _PyObject_VAR_SIZE(type, n_items);
        
    if (type->tp_flags & Py_TPFLAGS_HAVE_GC)
        op = _PyObject_GC_Malloc(size);
    else
        op = (PyObject *)PyObject_MALLOC(size);

    if (op == NULL)
        return PyErr_NoMemory();

    memset(op, '\0', size);

    Py_SIZE(op) = n_items;
    Py_TYPE(op) = type;
    _Py_NewReference(op);

    if (type->tp_flags & Py_TPFLAGS_HEAPTYPE)
        Py_INCREF(type);
    
    if (type->tp_flags & Py_TPFLAGS_HAVE_GC)
        PyObject_GC_Track(op);
    
    return op;
}

static PyObject*
datatuple_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *op;
    Py_ssize_t n_slots, n_items;
    PyObject *tmp;
    Py_ssize_t n, n_args;
    PyObject **items, **pp;
    PyObject *v;

    if (PyTuple_CheckExact(args)) {
        tmp = args;
        Py_INCREF(args);
    } else {
        tmp = PySequence_Tuple(args);
        if (tmp == NULL) {
            return NULL;
        }
    }

    n_args = PyTuple_GET_SIZE(tmp);

    n_slots = datatuple_numslots(type);
    n_items = n_args - n_slots;
    if (n_items < 0) {
        PyErr_SetString(PyExc_TypeError,
                        "number of the arguments should be greater or equal than the number of slots");
        Py_DECREF(tmp);
        return NULL;            
    }

    op = type->tp_alloc(type, n_items);

    pp = ((PyTupleObject*)tmp)->ob_item;
    if (n_slots) {
        n = n_slots;
        items = datatuple_slots(op);
        while (n-- > 0) {
            v = *(pp++);
            Py_INCREF(v);
            *(items++) = v;
        }
    }

    if (n_items) {
        n = n_items;
        items = datatuple_items(type, op);
        while (n-- > 0) {
            v = *(pp++);
            Py_INCREF(v);
            *(items++) = v;
        }
    }

    Py_DECREF(tmp);
    
    if (kwds) {
        if (type->tp_dictoffset) {
            PyObject **dictptr = dataobject_dictptr(type, op);
            PyObject *dict;
            if (*dictptr)
                dict = *dictptr;
            else {
                dict = PyDict_New();
                *dictptr = dict;
            }

            if (PyDict_Update(dict, kwds) == -1) {
                PyErr_SetString(PyExc_TypeError, "__dict__ update is failed");
                return NULL;            
            }
        } else {
            PyErr_SetString(PyExc_TypeError, "invalid kwargs");
            return NULL;            
        }        
    }
    

    return op;
}

static int
datatuple_clear(PyObject *op)
{
    PyTypeObject *type = Py_TYPE(op);
    Py_ssize_t n_slots, n_items;
    PyObject **items;
    
    n_slots = datatuple_numslots(type);

    items = datatuple_slots(op);
    if (n_slots) {
        while (n_slots-- > 0) {
            PyObject *v;

            v = *items;
            Py_XDECREF(v);
            *items = NULL;
            items++;
        }
    }

    items = datatuple_items(type, op);
    n_items = Py_SIZE(op);
    if (n_items) {
        while (n_items-- > 0) {
            PyObject *v;

            v = *items;
            Py_XDECREF(v);
            *items = NULL;
            items++;
        }
    }

    if (type->tp_dictoffset) {
        PyObject **dictptr = dataobject_dictptr(type, op);
        if (dictptr && *dictptr)
            Py_CLEAR(*dictptr);
    }

    if (type->tp_weaklistoffset)
        PyObject_ClearWeakRefs(op);

    return 0;
}

static void
datatuple_dealloc(PyObject *op)
{
    PyTypeObject *tp = Py_TYPE(op); 

    if (PyType_IS_GC(tp)) {
        PyObject_GC_UnTrack(op);
    }

    datatuple_clear(op);

    tp->tp_free((PyObject *)op);
}

static int
datatuple_traverse(PyObject *op, visitproc visit, void *arg)
{
    Py_ssize_t n_slots, n_items;
    PyObject **items;
    PyTypeObject *type = Py_TYPE(op);

    n_slots = datatuple_numslots(type);
    n_items = Py_SIZE(op);

    if (n_slots) {
        items = datatuple_slots(op);
        while (n_slots--) {
            PyObject *v;

            v = *(items++);
            Py_VISIT(v);
        }
    }

    if (n_items) {
        items = datatuple_items(type, op);
        while (n_items--) {
            PyObject *v;

            v = *(items++);
            Py_VISIT(v);
        }
    }

    if (type->tp_dictoffset) {
        PyObject **dictptr = dataobject_dictptr(type, op);
        if (dictptr && *dictptr)
            Py_VISIT(*dictptr);
    }

    return 0;
}

static PyObject *
datatuple_slot(PyObject *op, Py_ssize_t i)
{
    PyTypeObject *type = Py_TYPE(op);
    PyObject **items;
    PyObject *v;

    Py_ssize_t n= datatuple_numslots(type);

    if (i < 0)
        i += n;
    if (i < 0 || i >= n) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }

    items = datatuple_slots(op);

    v = items[i];
    Py_INCREF(v);
    return v;
}

static int
datatuple_ass_slot(PyObject *op, Py_ssize_t i, PyObject *val)
{
    PyObject **items;
    PyTypeObject *type = Py_TYPE(op);
    PyObject* old_val;

    Py_ssize_t n = datatuple_numslots(type);


    if (i < 0)
        i += n;
    if (i < 0 || i >= n) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return -1;
    }

    items = datatuple_slots(op);

    items += i;

    old_val = *items;
    if (old_val)
        Py_DECREF(old_val);
    Py_INCREF(val);
    *items = val;
    return 0;
}

static PyObject *
datatuple_item(PyObject *op, Py_ssize_t i)
{
    PyTypeObject *type = Py_TYPE(op);
    PyObject **items;
    PyObject *v;

    Py_ssize_t n_slots = datatuple_numslots(type);
    Py_ssize_t n_items = datatuple_numitems(op);
    Py_ssize_t n = n_slots + n_items;

    if (i < 0)
        i += n;
    if (i < 0 || i >= n) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return NULL;
    }

    if (i < n_slots) {
        items = datatuple_slots(op);
    } else {
        items = datatuple_items(type, op);
        i -= n_slots;
    }

    v = items[i];
    Py_INCREF(v);
    return v;
}

static int
datatuple_ass_item(PyObject *op, Py_ssize_t i, PyObject *val)
{
    Py_ssize_t n = Py_SIZE(op);
    PyObject **items;
    PyTypeObject *type = Py_TYPE(op);
    PyObject* old_val;

    Py_ssize_t n_slots = datatuple_numslots(type);
    Py_ssize_t n_items = datatuple_numitems(op);

    n = n_slots + n_items;

    if (i < 0)
        i += n;
    if (i < 0 || i >= n) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return -1;
    }

    if (i < n_slots) {
        items = datatuple_slots(op);
    } else {
        items = datatuple_items(type, op);
        i -= n_slots;
    }

    items += i;

    old_val = *items;
    if (old_val)
        Py_DECREF(old_val);
    Py_INCREF(val);
    *items = val;
    return 0;
}

static PyObject*
datatuple_subscript(PyObject* op, PyObject* item)
{
    return PyObject_GetAttr(op, item);
}

static int
datatuple_ass_subscript(PyObject* op, PyObject* item, PyObject *val)
{
    return PyObject_SetAttr(op, item, val);
}

static int
datatuple_ass_subscript2(PyObject* op, PyObject* item, PyObject *val)
{
    if (PyIndex_Check(item)) {
        Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return -1;

        return datatuple_ass_item(op, i, val); 
    } else 
        return PyObject_SetAttr(op, item, val);
}

static PyObject*
datatuple_subscript2(PyObject* op, PyObject* item)
{
    if (PyIndex_Check(item)) {
        Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return NULL;

        return datatuple_item(op, i); 
    } else 
        return PyObject_GetAttr(op, item);
}


PyDoc_STRVAR(datatuple_len_doc,
"T.__len__() -- len of T");

static Py_ssize_t
datatuple_len(PyObject *op)
{
    PyTypeObject *type = Py_TYPE(op);
    Py_ssize_t n;

    n = datatuple_numslots(type);
    if (type->tp_itemsize)
        n += Py_SIZE(op);

    return n;
}

PySequenceMethods datatuple_as_sequence0 = {
    (lenfunc)datatuple_len,                          /* sq_length */
    0,                                              /* sq_concat */
    0,                                              /* sq_repeat */
    0,                    /* sq_item */
    0,                                              /* sq_slice */
    0,             /* sq_ass_item */
    0,                                              /* sq_ass_slice */
    0,                                              /* sq_contains */
};


PySequenceMethods datatuple_as_sequence = {
    (lenfunc)datatuple_len,                          /* sq_length */
    0,                                              /* sq_concat */
    0,                                              /* sq_repeat */
    (ssizeargfunc)datatuple_item,                    /* sq_item */
    0,                                              /* sq_slice */
    (ssizeobjargproc)datatuple_ass_item,             /* sq_ass_item */
    0,                                              /* sq_ass_slice */
    0,                                              /* sq_contains */
};

PyMappingMethods datatuple_as_mapping0 = {
    (lenfunc)datatuple_len,                          /* mp_len */
    0,                 /* mp_subscr */
    0,          /* mp_ass_subscr */
};

PyMappingMethods datatuple_as_mapping = {
    (lenfunc)datatuple_len,                          /* mp_len */
    (binaryfunc)datatuple_subscript,                 /* mp_subscr */
    (objobjargproc)datatuple_ass_subscript,          /* mp_ass_subscr */
};

PyMappingMethods datatuple_as_mapping2 = {
    (lenfunc)datatuple_len,                          /* mp_len */
    (binaryfunc)datatuple_subscript2,                 /* mp_subscr */
    (objobjargproc)datatuple_ass_subscript2,          /* mp_ass_subscr */
};

PyDoc_STRVAR(datatuple_copy_doc,
"T.__copy__() -- copy of T");

static PyObject *
datatuple_copy(PyObject* op)
{
    PyTypeObject *type = Py_TYPE(op);
    PyObject *new_op = type->tp_alloc(type, Py_SIZE(op));

    Py_ssize_t i, n = datatuple_len(op);
    
    for(i=0; i<n; i++) {
        PyObject *v;

        v = datatuple_item(op, i);
        if (!v) {
            Py_DECREF(new_op);
            return NULL;
        }
        if (datatuple_ass_item(new_op, i, v) < 0) {
            Py_DECREF(v);
            Py_DECREF(new_op);
            return NULL;            
        }
        Py_DECREF(v);
    }

    if (type->tp_dictoffset) {
        PyObject **dictptr = dataobject_dictptr(type, op);
        PyObject *dict = *dictptr;
        
        PyObject **new_dictptr = dataobject_dictptr(type, new_op);
        PyObject *new_dict = *new_dictptr;
        
        if (dict && !new_dict) {
            new_dict = PyDict_New();
            if (!new_dict) {
                PyErr_SetString(PyExc_TypeError, "failed to create new dict");
                return NULL;                                    
            }
            *new_dictptr = new_dict;                        
        }
        
        if (dict) {
            if (PyDict_Update(new_dict, dict) < 0) {
                PyErr_SetString(PyExc_TypeError, "dict update failed");
                return NULL;                        
            }
        }        
    }

    return new_op;
}

// PyDoc_STRVAR(datatuple_getnewargs_doc,
// "T.__getnewargs__()");

// static PyObject *
// datatuple_getnewargs(PyObject *op)
// {
//     PyTypeObject *type = Py_TYPE(op);
//     Py_ssize_t i, n_slots = datatuple_numslots(type);
//     Py_ssize_t n_items = Py_SIZE(op);
//     PyObject *args, *tail;
//     PyObject *v;

//     args = PyTuple_New(n_slots+1);
//     if (args == NULL)
//         return NULL;
    
//     for (i=0; i<n_slots; i++) {
//         v = datatuple_slot(op, i);
//         if (!v) {
//             Py_DECREF(args);
//             return NULL;
//         }
//         PyTuple_SET_ITEM(args, i, v);
//     }

//     tail = PyTuple_New(n_items);
//     if (tail == NULL) {
//         Py_DECREF(args);
//         return NULL;
//     }

//     if (n_items > 0) {
//         for (i=0; i<n_items; i++) {
//             v = datatuple_item(op, n_slots+i);
//             if (!v) {
//                 Py_DECREF(args);
//                 return NULL;
//             }
//             PyTuple_SET_ITEM(tail, i, v);
//         }
//     }
    
//     PyTuple_SET_ITEM(args, n_slots, tail);
    
    
//     return args;
// }

// PyDoc_STRVAR(datatuple_reduce_doc,
// "T.__reduce__()");

// static PyObject *
// datatuple_reduce(PyObject *ob)
// {
//     PyObject *args;
//     PyObject *result;
//     PyTypeObject *tp = Py_TYPE(ob);
//     PyObject *kw = NULL;
//     PyObject **dictptr;

//     args = datatuple_getnewargs(ob);
//     if (args == NULL)
//         return NULL;

//     if (tp->tp_dictoffset) {
//         dictptr = dataobject_dictptr(tp, ob);
//         if (dictptr) {
//             kw = *dictptr;
//             if (kw) Py_INCREF(kw);
//         }
//     }
//     if (kw) {
//         result = PyTuple_Pack(3, Py_TYPE(ob), args, kw);
// //         Py_DECREF(kw);
//     } else
//         result = PyTuple_Pack(2, Py_TYPE(ob), args);
// //     Py_DECREF(args);

//     return result;
// }


static PyMethodDef datatuple_methods[] = {
    {"__copy__", (PyCFunction)datatuple_copy, METH_NOARGS, datatuple_copy_doc},
    {"__len__", (PyCFunction)datatuple_len, METH_NOARGS, datatuple_len_doc},
    {"__sizeof__",      (PyCFunction)dataobject_sizeof, METH_NOARGS, dataobject_sizeof_doc},
    {"__reduce__",      (PyCFunction)dataobject_reduce, METH_NOARGS, dataobject_reduce_doc},
    {"__getstate__",      (PyCFunction)dataobject_getstate, METH_NOARGS, dataobject_getstate_doc},
    {"__getnewargs__",      (PyCFunction)dataobject_getnewargs, METH_NOARGS, dataobject_getnewargs_doc},
    {"__setstate__",      (PyCFunction)dataobject_setstate, METH_O, dataobject_setstate_doc},
    {NULL}
};

static PyTypeObject PyDataTuple_Type = {
    PyVarObject_HEAD_INIT(DEFERRED_ADDRESS(&PyType_Type), 0)
    "recordclass._dataobject.datatuple",        /* tp_name */
    sizeof(PyVarObject),                   /* tp_basicsize */
    sizeof(PyObject*),                    /* tp_itemsize */
    /* methods */
    (destructor)datatuple_dealloc,           /* tp_dealloc */
    0,                                      /* tp_print */
    0,                                      /* tp_getattr */
    0,                                      /* tp_setattr */
    0,                                      /* tp_reserved */
    dataobject_repr,                                      /* tp_repr */
    0,                                      /* tp_as_number */
    0,                  /* tp_as_sequence */
    &datatuple_as_mapping0,                   /* tp_as_mapping */
    dataobject_hash,                          /* tp_hash */
    0,                                      /* tp_call */
    0,                                      /* tp_str */
    PyObject_GenericGetAttr,                /* tp_getattro */
    PyObject_GenericSetAttr,                /* tp_setattro */
    0,                                      /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_VERSION_TAG|Py_TPFLAGS_BASETYPE,
                                            /* tp_flags */
    dataobject_doc,                           /* tp_doc */
    0,                      /* tp_traverse */
    0,                         /* tp_clear */
    dataobject_richcompare,                   /* tp_richcompare */
    0,                                      /* tp_weaklistoffset*/
    dataobject_iter,                                      /* tp_iter */
    0,                                      /* tp_iternext */
    datatuple_methods,                       /* tp_methods */
    0,                                      /* tp_members */
    0,                                      /* tp_getset */
    &PyDataObject_Type,                                      /* tp_base */
    0,                                      /* tp_memoryslots */
    0,                                      /* tp_descr_get */
    0,                                      /* tp_descr_set */
    0,                                      /* tp_memoryslotsoffset */
    0,                                      /* tp_init */
    datatuple_alloc,                         /* tp_alloc */
    datatuple_new,                           /* tp_new */
    dataobject_free,                          /* tp_free */
    0                                       /* tp_is_gc */
};

//////////////////////////////////////////////////

static PyObject*
do_getitem(PyObject *op, Py_ssize_t i) {
    PyTypeObject *type = Py_TYPE(op);
    
    if (type->tp_itemsize)
        return datatuple_item(op, i);
    else
        return dataobject_item(op, i);
}

static Py_ssize_t
do_getlen(PyObject *op) {
    PyTypeObject *type = Py_TYPE(op);
    
    if (type->tp_itemsize)
        return datatuple_len(op);
    else
        return dataobject_len(op);

}


//////////////////////////////////////////////////////////////////////////

static PyObject* dataobject_iter(PyObject *seq);

//////////////////////////////////////////////////////////////////////////

// /*********************** DataObjectWRef **************************/

// typedef struct {
//     PyObject_HEAD
//     PyObject *value;
// } dataobject_weakref;

// static PyObject *
// dataobject_weakref_new(PyObject *value)
// {
//     dataobject_weakref *wref;

//     wref = PyObject_New(dataobject_weakref, &PyDataObjectWRef_Type);
//     if (wref == NULL)
//         return NULL;
//     wref->value = value;
//     if (value == NULL)
//         value = Py_None;
//     Py_INCREF(value);
//     return (PyObject *)wref;
// }

// static void
// dataobject_weakref_dealloc(dataobject_weakref *wref)
// {
//     Py_CLEAR(wref->value);
//     PyObject_Del(wref);
// }

// static PyObject*
// dataobject_weakref_value(PyObject *self)
// {
//     PyObject *value;
//     value = ((struct dataobject_weakref*)self)->value;
//     Py_INCREF(value);
//     return value;
// }

// static int
// dataobject_weakref_set_value(PyObject *self, PyObject *val)
// {
//     PyObject *value = ((struct dataobject_weakref*)self)->value;
//     Py_XDECREF(value);
//     Py_INCREF(val);
//     ((struct dataobject_weakref*)self)->value = val;
//     return 0;
// }

// static PyGetSetDef dataobject_weakref_getsets[] = {
//     {"value", (getter)dataobject_weakref_value, (setter)dataobject_weakref_set_value, NULL},
//     {0}
// };

// PyTypeObject PyDataObjectWRef_Type = {
//     PyVarObject_HEAD_INIT(DEFERRED_ADDRESS(&PyType_Type), 0)
//     "recordclass._dataobject.dataobject_weakref",                           /* tp_name */
//     sizeof(dataobject_weakref),                    /* tp_basicsize */
//     0,                                          /* tp_itemsize */
//     /* methods */
//     (destructor)dataobject_weakref_dealloc,              /* tp_dealloc */
//     0,                                          /* tp_print */
//     0,                                          /* tp_getattr */
//     0,                                          /* tp_setattr */
//     0,                                          /* tp_reserved */
//     0,                                          /* tp_repr */
//     0,                                          /* tp_as_number */
//     0,                                          /* tp_as_sequence */
//     0,                                          /* tp_as_mapping */
//     0,                                          /* tp_hash */
//     0,                                          /* tp_call */
//     0,                                          /* tp_str */
//     PyObject_GenericGetAttr,                    /* tp_getattro */
//     0,                                          /* tp_setattro */
//     0,                                          /* tp_as_buffer */
//     Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,                         /* tp_flags */
//     0,                                          /* tp_doc */
//     0,     /* tp_traverse */
//     0,             /* tp_clear */
//     0,                                          /* tp_richcompare */
//     0,                                          /* tp_weaklistoffset */
//     PyObject_SelfIter,                          /* tp_iter */
//     (iternextfunc)dataobjectiter_next,         /* tp_iternext */
//     dataobjectiter_methods,                    /* tp_methods */
//     0,                                      /* tp_members */
//     dataobject_weakref_getsets,                                      /* tp_getset */
//     0,                                      /* tp_base */
//     0,                                      /* tp_memoryslots */
//     0,                                      /* tp_descr_get */
//     0,                                      /* tp_descr_set */
//     0,                                      /* tp_memoryslotsoffset */
//     0,                                      /* tp_init */
//     0,                         /* tp_alloc */
//     dataobject_weakref_new,                           /* tp_new */
//     PyObject_Del,                          /* tp_free */
//     0                                       /* tp_is_gc */
// };


/*********************** DataObject Iterator **************************/

static PyObject* dataobject_iter(PyObject *seq);

typedef struct {
    PyObject_HEAD
    Py_ssize_t it_index, it_len;
    PyObject *it_seq; /* Set to NULL when iterator is exhausted */
} dataobjectiterobject;

static void
dataobjectiter_dealloc(dataobjectiterobject *it)
{
    Py_CLEAR(it->it_seq);
    PyObject_Del(it);
}

static PyObject *
dataobjectiter_next(dataobjectiterobject *it)
{
    PyObject *item;
    PyObject *op = it->it_seq;
    
    if (it->it_index < it->it_len) {
        if (Py_TYPE(op)->tp_itemsize)
            item = datatuple_item(op, it->it_index);
        else
            item = dataobject_item(op, it->it_index);
        it->it_index++;
        return item;
    }

    Py_DECREF(it->it_seq);
    it->it_seq = NULL;
    return NULL;
}

static PyObject *
dataobjectiter_len(dataobjectiterobject *it)
{
    Py_ssize_t len = 0;
    if (it->it_seq)
        len = it->it_len - it->it_index;
    return PyLong_FromSsize_t(len);
}

PyDoc_STRVAR(length_hint_doc, "Private method returning an estimate of len(list(it)).");

static PyObject *
dataobjectiter_reduce(dataobjectiterobject *it, PyObject *Py_UNUSED(ignore))
{
#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 6
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

PyDoc_STRVAR(dataobjectiter_reduce_doc, "D.__reduce__()");

static PyObject *
dataobjectiter_setstate(dataobjectiterobject *it, PyObject *state)
{
    Py_ssize_t index;

#if PY_MAJOR_VERSION >= 3    
    index = PyLong_AsSsize_t(state);
#else
    index = PyNumber_AsSsize_t(state, NULL);
#endif
    if (index == -1 && PyErr_Occurred())
        return NULL;
    if (it->it_seq != NULL) {
        if (index < 0)
            index = 0;
        else if (index > it->it_len)
            index = it->it_len; /* exhausted iterator */
        it->it_index = index;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(setstate_doc, "Set state information for unpickling.");

static PyMethodDef dataobjectiter_methods[] = {
    {"__length_hint__", (PyCFunction)dataobjectiter_len, METH_NOARGS, length_hint_doc},
    {"__reduce__",      (PyCFunction)dataobjectiter_reduce, METH_NOARGS, dataobjectiter_reduce_doc},
    {"__setstate__",    (PyCFunction)dataobjectiter_setstate, METH_O, setstate_doc},
    {NULL,              NULL}           /* sentinel */
};

PyTypeObject PyDataObjectIter_Type = {
    PyVarObject_HEAD_INIT(DEFERRED_ADDRESS(&PyType_Type), 0)
    "recordclass._dataobject.dataobject_iterator",                           /* tp_name */
    sizeof(dataobjectiterobject),                    /* tp_basicsize */
    0,                                          /* tp_itemsize */
    /* methods */
    (destructor)dataobjectiter_dealloc,              /* tp_dealloc */
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
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,                         /* tp_flags */
    0,                                          /* tp_doc */
    0,     /* tp_traverse */
    0,             /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    PyObject_SelfIter,                          /* tp_iter */
    (iternextfunc)dataobjectiter_next,         /* tp_iternext */
    dataobjectiter_methods,                    /* tp_methods */
    0,
};

static PyObject *
dataobject_iter(PyObject *seq)
{
    dataobjectiterobject *it;

    it = PyObject_New(dataobjectiterobject, &PyDataObjectIter_Type);
    if (it == NULL)
        return NULL;
    it->it_index = 0;
    it->it_seq = seq;
    it->it_len = do_getlen(seq);
    Py_INCREF(seq);
    return (PyObject *)it;
}

////////////////////////////////////////////////////////////////////////

struct dataslotgetset_object {
    PyObject_HEAD
    Py_ssize_t offset;
    short readonly;
};

static PyMethodDef dataslotgetset_methods[] = {
//   {"__set_name__", dataslotgetset_setname, METH_VARARGS, dataslotgetset_setname_doc},
  {0, 0, 0, 0}
};

static PyObject* dataslotgetset_new(PyTypeObject *t, PyObject *args, PyObject *k) {    
    struct dataslotgetset_object *ob = NULL;
    PyObject *item;
    Py_ssize_t len, offset;
    int readonly;

    len = Py_SIZE(args);
    if (len == 0 || len > 2) {
        PyErr_SetString(PyExc_TypeError, "number of args is 1 or 2");
        return NULL;        
    }

    item = PyTuple_GET_ITEM(args, 0);
    if (len == 2)
        readonly = PyObject_IsTrue(PyTuple_GET_ITEM(args, 1));
    else
        readonly = 0;

    offset = PyNumber_AsSsize_t(item, PyExc_IndexError);
    if (offset == -1 && PyErr_Occurred()) {
        Py_DECREF(ob);
        return NULL;
    }

    ob = (struct dataslotgetset_object*)PyBaseObject_Type.tp_new(t, PyTuple_New(0), 0);    
    if (ob == NULL)
        return NULL;

    ob->readonly = readonly;
    ob->offset = offset;
    return (PyObject*)ob;
}

static void dataslotgetset_dealloc(PyObject *o) {
    PyObject_Del(o);
}

#define dataobject_item_by_offset(op, offset) (*((PyObject**)((char*)op + offset)))
#define dataobject_ass_item_by_offset(op, offset, val) (*((PyObject**)((char*)op + offset))=val)

#define self_igs ((struct dataslotgetset_object *)self)

static PyObject* dataslotgetset_get(PyObject *self, PyObject *obj, PyObject *type) {
    PyObject *v;

    if (obj == NULL || obj == Py_None) {
        Py_INCREF(self);
        return self;
    }

    v = dataobject_item_by_offset(obj, self_igs->offset);
    Py_INCREF(v);
    return v;
}

static int dataslotgetset_set(PyObject *self, PyObject *obj, PyObject *value) {
    PyObject *v;

    if (value == NULL) {
        PyErr_SetString(PyExc_NotImplementedError, "__delete__");
        return -1;
    }

    if (obj == NULL || obj == Py_None)
        return 0;

    if (self_igs->readonly) {
        PyErr_SetString(PyExc_TypeError, "item is readonly");
        return -1;
    }

    v = dataobject_item_by_offset(obj, self_igs->offset);
    Py_DECREF(v);
    
    dataobject_ass_item_by_offset(obj, self_igs->offset, value);
    
    Py_INCREF(value);
    return 0;
}

static PyObject*
dataslotgetset_offset(PyObject *self)
{
    return PyLong_FromSsize_t(((struct dataslotgetset_object*)self)->offset);
}

static PyObject*
dataslotgetset_readonly(PyObject *self)
{
    return PyBool_FromLong((long)(((struct dataslotgetset_object*)self)->offset));
}

static PyGetSetDef dataslotgetset_getsets[] = {
    {"offset", (getter)dataslotgetset_offset, NULL, NULL},
    {"readonly", (getter)dataslotgetset_readonly, NULL, NULL},
    {0}
};

static PyTypeObject PyDataSlotGetSet_Type = {
    PyVarObject_HEAD_INIT(DEFERRED_ADDRESS(&PyType_Type), 0)
    "recordclass._dataobject.dataslotgetset", /*tp_name*/
    sizeof(struct dataslotgetset_object), /*tp_basicsize*/
    0, /*tp_itemsize*/
    dataslotgetset_dealloc, /*tp_dealloc*/
    0, /*tp_print*/
    0, /*tp_getattr*/
    0, /*tp_setattr*/
    0, /*reserved*/
    0, /*tp_repr*/
    0, /*tp_as_number*/
    0, /*tp_as_sequence*/
    0, /*tp_as_mapping*/
    0, /*tp_hash*/
    0, /*tp_call*/
    0, /*tp_str*/
    0, /*tp_getattro*/
    0, /*tp_setattro*/
    0, /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE, /*tp_flags*/
    0, /*tp_doc*/
    0, /*tp_traverse*/
    0, /*tp_clear*/
    0, /*tp_richcompare*/
    0, /*tp_weaklistoffset*/
    0, /*tp_iter*/
    0, /*tp_iternext*/
    dataslotgetset_methods, /*tp_methods*/
    0, /*tp_members*/
    dataslotgetset_getsets, /*tp_getset*/
    0, /*tp_base*/
    0, /*tp_dict*/
    dataslotgetset_get, /*tp_descr_get*/
    dataslotgetset_set, /*tp_descr_set*/
    0, /*tp_dictoffset*/
    0, /*tp_init*/
    0, /*tp_alloc*/
    dataslotgetset_new, /*tp_new*/
    0, /*tp_free*/
    0, /*tp_is_gc*/
};


//////////////////// datatype ////////////////////////////////////////////

// static int _get_bool_value(PyObject *options, const char *name) {
//         PyObject* b;
        
//         b = PyMapping_GetItemString(options, name);
//         if (b && PyObject_IsTrue(b))
//             return 1;
//         else
//             return 0;
// }


// static PyObject*
// datatype_new(PyTypeObject type, PyObject *typename, PyObject *bases, PyObject *ns) {

//     PyObject *options;
//     PyObject *annotations;
//     PyObject *fields;
//     int varsize = 0;
//     int has_fields = 1;
//     int fields_from_annotations = 0;
//     int is_int = 0;
//     int sequence, mapping, iterable;
//     int n_fields;
    
//     // options = ns.pop('__otpions__', {})
//     options = PyMapping_GetItemString(ns, "__options__");
//     // if options:
//     if (options) {
//         // varsize = options.get('varsize', False)
//         varsize = _get_bool_value(options, "varsize");
//         // sequence = options.get('sequence', False)
//         sequence = _get_bool_value(options, "sequence");
//         // mapping = options.get('mapping', False)
//         mapping = _get_bool_value(options, "mapping");
//         // iterable = options.get('iterable', False)
//         iterable = _get_bool_value(options, "iterable");

//         PyMapping_DelItemString(ns, "__options__");
//     }
    
//     // if not bases:
//     if (!bases || !PySequence_Length(bases)) {
//         Py_XDECREF(bases);
//         // if varsize:
//         if (varsize) {
//             if (bases) {
//                 // bases = (datatuple,)
//                 bases = PyTuple_Pack(1, (PyObject*)&PyDataTuple_Type);
//             } else { 
//                 // bases = (dataobject,)
//                 bases = PyTuple_Pack(1, (PyObject*)&PyDataObject_Type);
//             }
//         }
//     } else {
//         // base0 = bases[0]
//         PyObject* base0 = PyTuple_GET_ITEM(bases, 0);
//         // if issubclass(base0, dataobject)
//         if (PyObject_IsSubclass(base0, (PyObject*)&PyDataObject_Type)) {
//             // varsize = False
//             varsize = 0;
//         } else {
//             // elif isdubclaass(base0, datatuple)
//             if (PyObject_IsSubclass(base0, (PyObject*)&PyDataTuple_Type)) {
//                 // varsize = True
//                 varsize = 1;
//             } else { 
//                 // else:
//                 //.   raise TypeError('...')
//                 PyErr_SetString(PyExc_TypeError, 
//                         "First base class should be instance of dataobject or datatuple");
//                 return NULL;
//             }
//         }
//     }
    
//     // annotation = ns.get('__annotations__', {})
//     annotations = PyMapping_GetItemString(ns, "__annotations__");
    
//     // fields = ns.get('__fields__', None)
//     fields = PyMapping_GetItemString(ns, "__fields__");
//     // if fields is not None:
//     if (fields && PyIndex_Check(fields)) {
//             is_int = 1;
//             has_fields = 0;
//             n_fields = PyNumber_AsSsize_t(fields, &PyExc_TypeError);
//             sequence = 1;
//             iterable = 1;
//     }
//     if (fields && PySequence_Check(fields)) {
//         PyObject *lst, *o;
//         Py_ssize_t i, size = PySequence_Length(fields);

//         // fields = list(fields)
//         lst = PyList_New(size);
//         for (i=0; i<size; i++) {
//             o = PySequence_GetItem(fields, i);
//             PyList_Append(lst, o);
//         }
//         Py_DECREF(fields);
//         fields = lst;
//         has_fields = 1;
//         is_int = 0;
//         n_fields = size;
//     }
//     if (!fields && annotations) {
//         // elif annotations:
//         PyObject *keys, *iterkeys;
//         Py_ssize_t i, size;

//         size = PyMapping_Length(annotations);
//         fields = PyList_New(size);
//         keys = PyMapping_Keys(annotations);
//         iterkeys = PyObject_GetIter(keys);

//         // fields = [fn for fn in annotations]
//         for (i=0; i<size; i++) {
//             PyObject *o;

//             o = PyIter_Next(iterkeys);
//             Py_INCREF(o);
//             PyList_SET_ITEM(fields, i, o);
//         }

//         Py_DECREF(keys);
//         Py_DECREF(iterkeys);

//         has_fields = 1;
//         is_int = 0;
//         n_fields = size;
//     }
    
//     if (varsize) {
//         sequence = 1;
//         iterable = 1;
//     }
    
//     if (sequence || mapping)
//         iterable = 1;
        
//     if (has_fields) {
// //     if annotations:
// //         annotations = {fn:annotations[fn] for fn in fields if fn in annotations}
//         if (annotations) {
//             //PyObject 
//         }
    
//     }
    
//     return NULL;
    
// }

//////////////////// module level functions //////////////////////////////

static PyObject*
_collection_protocol(PyObject *cls, PyObject *sequence, PyObject *mapping, PyObject *readonly) {
    PyTypeObject *tp;
    PyTypeObject *tp_base;
    int sq, mp, ro;

    tp = (PyTypeObject*)cls;
    sq = PyObject_IsTrue(sequence);
    mp = PyObject_IsTrue(mapping);
    ro = PyObject_IsTrue(readonly);
    
    tp_base = tp->tp_base;
    
    if (!tp->tp_itemsize) {
        if ((tp_base != &PyDataObject_Type) && !PyType_IsSubtype(tp_base, &PyDataObject_Type)) {
            PyErr_SetString(PyExc_TypeError, "the type should be dataobject or it's subtype");
            return NULL;            
        }
    } else {
        if ((tp_base != &PyDataTuple_Type) && !PyType_IsSubtype(tp_base, &PyDataTuple_Type)) {
            PyErr_SetString(PyExc_TypeError, "the type should be datatuple or it's subtype");
            return NULL;            
        }
    
    }

    if (!tp->tp_itemsize) {
        if (sq) {
            if (mp) {
                tp->tp_as_sequence = NULL;
                tp->tp_as_mapping = &dataobject_as_mapping2;
                if (ro)
                   tp->tp_as_mapping->mp_ass_subscript = NULL;
            } else {
                tp->tp_as_sequence = &dataobject_as_sequence;
                if (ro)
                   tp->tp_as_sequence->sq_ass_item = NULL;            
            }
        } else {
            if (mp) {
                tp->tp_as_sequence = NULL;
                tp->tp_as_mapping = &dataobject_as_mapping;
                if (ro)
                   tp->tp_as_mapping->mp_ass_subscript = NULL;            
            } else {
                tp->tp_as_sequence = &dataobject_as_sequence0;
                tp->tp_as_mapping = &dataobject_as_mapping0;
            }        
        }
    } else {
        if (sq) {
            if (mp) {
                tp->tp_as_sequence = NULL;
                tp->tp_as_mapping = &datatuple_as_mapping2;
                if (ro)
                   tp->tp_as_mapping->mp_ass_subscript = NULL;
            } else {
                tp->tp_as_sequence = &datatuple_as_sequence;
                if (ro)
                   tp->tp_as_sequence->sq_ass_item = NULL;            
            }
        } else {
            if (mp) {
                tp->tp_as_sequence = NULL;
                tp->tp_as_mapping = &datatuple_as_mapping;
                if (ro)
                   tp->tp_as_mapping->mp_ass_subscript = NULL;            
            } else {
                tp->tp_as_sequence = &datatuple_as_sequence0;
                tp->tp_as_mapping = &datatuple_as_mapping0;
            }        
        }
    }

    Py_RETURN_NONE;
}

static PyObject*
_set_hashable(PyObject *cls, PyObject *hashable) {
    PyTypeObject *tp;
    int state;

    tp = (PyTypeObject*)cls;
    state = PyObject_IsTrue(hashable);

    if (state)
        tp->tp_hash = dataobject_hash;
    else
        tp->tp_hash = NULL;

    Py_RETURN_NONE;
}

static PyObject*
_set_iterable(PyObject *cls, PyObject *iterable) {
    PyTypeObject *tp;
    int state;

    tp = (PyTypeObject*)cls;
    state = PyObject_IsTrue(iterable);

    if (state)
        tp->tp_iter = dataobject_iter;
    else
        tp->tp_iter = NULL;

    Py_RETURN_NONE;
}

static void
__fix_type(PyObject *tp, PyTypeObject *meta) {
    PyObject *val;
    
    if (tp->ob_type != meta) {
        val = (PyObject*)tp->ob_type;
        Py_XDECREF(val);
        tp->ob_type = meta;
        Py_INCREF(meta);
        PyType_Modified((PyTypeObject*)tp);
    }     
}

static PyObject*
_set_dictoffset(PyObject *cls, PyObject *add_dict) {
    PyTypeObject *tp;
    int state;

    tp = (PyTypeObject*)cls;
    state = PyObject_IsTrue(add_dict);

    if (!PyObject_IsInstance((PyObject*)tp, (PyObject*)&PyType_Type)) {
        PyErr_SetString(PyExc_TypeError, "argument is not a subtype of the type");        
        return NULL;
    }
    
    if (!tp->tp_dictoffset && state) {
        tp->tp_dictoffset = tp->tp_basicsize;
        tp->tp_basicsize += sizeof(PyObject*);
        if (tp->tp_weaklistoffset)
            tp->tp_weaklistoffset = tp->tp_basicsize;
    }
    if (tp->tp_dictoffset && !state) {
        tp->tp_dictoffset = 0;
        tp->tp_basicsize -= sizeof(PyObject*);
        if (tp->tp_weaklistoffset)
            tp->tp_weaklistoffset = tp->tp_basicsize;
    }

    Py_RETURN_NONE;
}

static PyObject*
_set_weaklistoffset(PyObject *cls, PyObject* add_weakref) {
    PyTypeObject *tp;
    int state;

    tp = (PyTypeObject*)cls;
    if (!PyObject_IsInstance((PyObject*)tp, (PyObject*)&PyType_Type)) {
        PyErr_SetString(PyExc_TypeError, "argument is not a subtype of the type");        
        return NULL;
    }

    state = PyObject_IsTrue(add_weakref);

    if (!tp->tp_weaklistoffset && state) {
        tp->tp_weaklistoffset = tp->tp_basicsize;
        tp->tp_basicsize += sizeof(PyObject*);
    }
    if (tp->tp_weaklistoffset && !state) {
        tp->tp_weaklistoffset = 0;
        tp->tp_basicsize -= sizeof(PyObject*);
        if (tp->tp_dictoffset)
            tp->tp_dictoffset = tp->tp_basicsize;
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(_dataobject_type_init_doc,
"Initialize dataobject subclass");

static PyObject* 
_dataobject_type_init(PyObject *module, PyObject *args) {
    PyObject *cls;

    PyTypeObject *tp;
    PyTypeObject *tp_base;
    PyObject *__dict__;
    int __init__, __new__;
    PyObject *fields;
    int n_fields;
    int has_fields;

    if (Py_SIZE(args) != 1) {
        PyErr_SetString(PyExc_TypeError, "number of arguments != 1");
        return NULL;
    }

    cls = PyTuple_GET_ITEM(args, 0);

    __dict__ = PyObject_GetAttrString(cls, "__dict__");

    fields = PyMapping_GetItemString(__dict__, "__fields__");
    if (!fields){
        PyErr_SetString(PyExc_TypeError, "__fields__ is missing");
        return NULL;    
    }
    
    if (PyTuple_Check(fields)) {
        n_fields = PyTuple_GET_SIZE(fields);
        has_fields = 1;
    } else {
        n_fields = PyNumber_AsSsize_t(fields, PyExc_IndexError);
        if (n_fields == -1 && PyErr_Occurred())
            return NULL;
        has_fields = 0;
    }
    
    tp = (PyTypeObject*)cls;
    tp_base = tp->tp_base;

    if (PyObject_IsSubclass((PyObject*)tp_base, (PyObject*)&PyDataTuple_Type)) {
            tp->tp_basicsize = sizeof(PyVarObject);    
            tp->tp_itemsize = sizeof(PyObject*);
    } else {
        if (PyObject_IsSubclass((PyObject*)tp_base, (PyObject*)&PyDataObject_Type)) {
            tp->tp_basicsize = sizeof(PyObject);
            tp->tp_itemsize = 0;
        } else {
            PyErr_SetString(PyExc_TypeError, 
                            "common base class should be dataobject, datatuple or subclass");        
            return NULL;
        }
    }

    if (n_fields >= 0) {
        tp->tp_basicsize += n_fields * sizeof(PyObject*);
    } else {
        PyErr_SetString(PyExc_TypeError, "number of fields should not be negative");        
        return NULL;    
    }

    tp->tp_dictoffset = 0;
    tp->tp_weaklistoffset = 0;

    tp->tp_alloc = tp_base->tp_alloc;

    __new__ = PyMapping_HasKeyString(__dict__, "__new__");

    if (!has_fields && !__new__)
        tp->tp_new = tp_base->tp_new;    

    tp->tp_dealloc = tp_base->tp_dealloc;
    tp->tp_free = tp_base->tp_free;
    
    __init__ = PyMapping_HasKeyString(__dict__, "__init__");
    if (!__init__) {
        if (tp_base->tp_init)
            tp->tp_init = tp_base->tp_init;
        else
            tp->tp_init = NULL;
    }
    
    tp->tp_flags |= Py_TPFLAGS_HEAPTYPE;
    
    if (tp->tp_flags & Py_TPFLAGS_HAVE_GC)
        tp->tp_flags &= ~Py_TPFLAGS_HAVE_GC;

    tp->tp_traverse = NULL;
    tp->tp_clear = NULL;
    tp->tp_is_gc = NULL;
    
    PyType_Modified(tp);

    Py_RETURN_NONE;
}

PyDoc_STRVAR(enable_gc_doc,
"Enable GC for specified (var)dataobject class");

static PyObject *
dataobject_enable_gc(PyObject *module, PyObject *args)
{
    PyObject *cls;
    PyTypeObject *type;

    if (Py_SIZE(args) > 1) {
        PyErr_SetString(PyExc_TypeError, "too many arguments");        
        return NULL;
    }

    cls = PyTuple_GET_ITEM(args, 0);
    
    if (!PyObject_IsInstance(cls, (PyObject*)&PyType_Type)) {
        PyErr_SetString(PyExc_TypeError, "Argument have to be an instance of type");        
        return NULL;
    }

    type = (PyTypeObject*)cls;
    type->tp_flags |= Py_TPFLAGS_HAVE_GC;
    if (type->tp_itemsize) {
        type->tp_traverse = datatuple_traverse;
        type->tp_clear = datatuple_clear;
    } else {
        type->tp_traverse = dataobject_traverse;
        type->tp_clear = dataobject_clear;
    }
    
    Py_INCREF(cls);
    return cls;
}

PyDoc_STRVAR(astuple_doc,
"Fast factory for creation of dataobject instances");

static PyObject *
_astuple(PyObject *op)
{
    Py_ssize_t i, n;
    PyObject *tpl;
    PyObject *v;

    n = do_getlen(op);
    tpl = PyTuple_New(n);
    for (i=0; i<n; i++) {
        v = do_getitem(op, i);
        PyTuple_SET_ITEM(tpl, i, v);
    }
    return tpl;
}

static PyObject *
astuple(PyObject *module, PyObject *args)
{
    PyObject *op;
    PyTypeObject *type;
    
    op = PyTuple_GET_ITEM(args, 0);
    type = Py_TYPE(op);

    if (type != &PyDataObject_Type && 
        !PyType_IsSubtype(type, &PyDataObject_Type)) {
            PyErr_SetString(PyExc_TypeError, "1st argument is not subclass of dataobject");        
            return NULL;        
    }    

    return _astuple(op);
}

PyDoc_STRVAR(clsconfig_doc,
"Configure some class aspects");

static PyObject *
clsconfig(PyObject *module, PyObject *args, PyObject *kw) {
    PyObject *cls = PyTuple_GET_ITEM(args, 0);
    PyObject *sequence = PyMapping_GetItemString(kw, "sequence");
    PyObject *mapping = PyMapping_GetItemString(kw, "mapping");
    PyObject *readonly = PyMapping_GetItemString(kw, "readonly");
    PyObject *use_dict = PyMapping_GetItemString(kw, "use_dict");
    PyObject *use_weakref = PyMapping_GetItemString(kw, "use_weakref");
    PyObject *iterable = PyMapping_GetItemString(kw, "iterable");
    PyObject *hashable = PyMapping_GetItemString(kw, "hashable");
    
    _collection_protocol(cls, sequence, mapping, readonly);
    _set_dictoffset(cls, use_dict);
    _set_weaklistoffset(cls, use_weakref);
    _set_hashable(cls, hashable);
    _set_iterable(cls, iterable);
    Py_XDECREF(sequence);
    Py_XDECREF(mapping);
    Py_XDECREF(readonly);
    Py_XDECREF(use_dict);
    Py_XDECREF(use_weakref);
    Py_XDECREF(iterable);
    Py_XDECREF(hashable);
    return cls;
}

//////////////////////////////////////////////////

PyDoc_STRVAR(dataobjectmodule_doc,
"dataobject module provide `dataobject` class.");

static PyMethodDef dataobjectmodule_methods[] = {
    {"astuple", astuple, METH_VARARGS, astuple_doc},
    {"enable_gc", dataobject_enable_gc, METH_VARARGS, enable_gc_doc},
    {"_dataobject_type_init", _dataobject_type_init, METH_VARARGS, _dataobject_type_init_doc},
    {"_clsconfig", (PyCFunction)clsconfig, METH_VARARGS | METH_KEYWORDS, clsconfig_doc},
    {0, 0, 0, 0}
};


#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef dataobjectmodule = {
  #if PY_VERSION_HEX < 0x03020000
    { PyObject_HEAD_INIT(NULL) NULL, 0, NULL },
  #else
    PyModuleDef_HEAD_INIT,
  #endif
    "recordclass._dataobject",
    dataobjectmodule_doc,
    -1,
    dataobjectmodule_methods,
    NULL,
    NULL,
    NULL,
    NULL
};
#endif

#if PY_MAJOR_VERSION >= 3
PyMODINIT_FUNC
PyInit__dataobject(void)
{
    PyObject *m;
    PyTypeObject *dtype;    
    
    m = PyState_FindModule(&dataobjectmodule);
    if (m) {
        Py_INCREF(m);
        return m;
    }    

    m = PyModule_Create(&dataobjectmodule);
    if (m == NULL)
        return NULL;
    
    if (PyType_Ready(&PyDataObject_Type) < 0)
        Py_FatalError("Can't initialize dataobject type");
    if (PyType_Ready(&PyDataTuple_Type) < 0)
        Py_FatalError("Can't initialize datatuple type");

    if (PyType_Ready(&PyDataObjectIter_Type) < 0)
        Py_FatalError("Can't initialize dataobjectiter type");

    if (PyType_Ready(&PyDataSlotGetSet_Type) < 0)
        Py_FatalError("Can't initialize dataslotgetset type");    

    Py_INCREF(&PyDataObject_Type);
    PyModule_AddObject(m, "dataobject", (PyObject *)&PyDataObject_Type);    
    Py_INCREF(&PyDataTuple_Type);
    PyModule_AddObject(m, "datatuple", (PyObject *)&PyDataTuple_Type);    

    Py_INCREF(&PyDataObjectIter_Type);
    PyModule_AddObject(m, "dataobject_iterator", (PyObject *)&PyDataObjectIter_Type);

    Py_INCREF(&PyDataSlotGetSet_Type);
    PyModule_AddObject(m, "dataslotgetset", (PyObject *)&PyDataSlotGetSet_Type);        

    dtype = (PyTypeObject*)_PyObject_GetObject("recordclass", "datatype");
    __fix_type((PyObject*)&PyDataObject_Type, dtype);
    __fix_type((PyObject*)&PyDataTuple_Type, dtype);    
    
    return m;
}
#else
PyMODINIT_FUNC
init_dataobject(void)
{
    PyObject *m;
    PyTypeObject *dtype;

    m = Py_InitModule3("recordclass._dataobject", dataobjectmodule_methods, dataobjectmodule_doc);
    if (m == NULL)
        return;
    Py_XINCREF(m);

    if (PyType_Ready(&PyDataObject_Type) < 0)
         Py_FatalError("Can't initialize dataobject type");
    if (PyType_Ready(&PyDataTuple_Type) < 0)
        Py_FatalError("Can't initialize datatuple type");

    if (PyType_Ready(&PyDataObjectIter_Type) < 0)
        Py_FatalError("Can't initialize dataobjectiter type");

    if (PyType_Ready(&PyDataSlotGetSet_Type) < 0)
        Py_FatalError("Can't initialize dataslotgetset type");

    Py_INCREF(&PyDataObject_Type);
    PyModule_AddObject(m, "dataobject", (PyObject *)&PyDataObject_Type);
    Py_INCREF(&PyDataTuple_Type);
    PyModule_AddObject(m, "datatuple", (PyObject *)&PyDataTuple_Type);    

    Py_INCREF(&PyDataObjectIter_Type);
    PyModule_AddObject(m, "dataobject_iterator", (PyObject *)&PyDataObjectIter_Type);

    Py_INCREF(&PyDataSlotGetSet_Type);
    PyModule_AddObject(m, "dataslotgetset", (PyObject *)&PyDataSlotGetSet_Type);        

    dtype = (PyTypeObject*)_PyObject_GetObject("recordclass", "datatype");
    __fix_type((PyObject*)&PyDataObject_Type, dtype);
    __fix_type((PyObject*)&PyDataTuple_Type, dtype);
    
    return;
}
#endif
