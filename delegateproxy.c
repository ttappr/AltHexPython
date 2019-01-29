/*******************************************************************************
 * MIT License
 *
 * Copyright (c) 2018 tmtappr@gmail.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 ******************************************************************************/

/**
 * A DelegateProxy wraps objects and provides Delegates for their methods
 * or functions. There are two ready-made DelegateProxy's provided by the
 * hexchat module: 'hexchat.synchronous' and 'hexchat.asynchronous'. Each
 * provides Delegates for the hexchat API. The synchronous DelegateProxy
 * provides synchronous Delegates for the API, and the asynchronous
 * DelegateProxy provides asynchronous Delegates. DelegateProxy's can be
 * created to wrap any arbitrary object if desired.
 */

#include "minpython.h"

/**
 * Instance object data.
 */
typedef struct {
    PyObject_HEAD
    PyObject *obj;
    PyObject *cache;
    int      is_async;
} DelegateProxyObj;

static int      DelegateProxy_init       (DelegateProxyObj *, PyObject *, 
                                                              PyObject *);
static void     DelegateProxy_dealloc    (DelegateProxyObj *);
static PyObject *DelegateProxy_getattro  (DelegateProxyObj *, PyObject *);
static PyObject *DelegateProxy_dir       (DelegateProxyObj *);
static PyObject *DelegateProxy_is_async  (DelegateProxyObj *, void *);
static PyObject *DelegateProxy_wrapped   (DelegateProxyObj *, void *);
static Py_hash_t DelegateProxy_hash      (DelegateProxyObj *);
static PyObject *DelegateProxy_cmp       (DelegateProxyObj *, PyObject *, int);
static PyObject *DelegateProxy_repr      (DelegateProxyObj *, PyObject *);

/**
 * DelegateProxy instance members.
 */
static PyMemberDef DelegateProxy_members[] = {
    { NULL }
};

/**
 * DelegateProxy methods.
 */
static PyMethodDef DelegateProxy_methods[] = {
    {"__dir__",   (PyCFunction)DelegateProxy_dir,      METH_NOARGS,
     "Returns attributes of DelegateProxy, which include the attributes of the "
     "wrapped object."},
    
    { NULL }
};

/**
 * Instance getters and setters.
 */
static PyGetSetDef DelegateProxy_accessors[] = {
    { "is_async",    (getter)DelegateProxy_is_async, (setter)NULL,
      "If async is True, the methods of the proxy will return AsyncResult "
      "objects. If False, the methods return the same objects as the "
      "internal methods.", NULL },
      
    { "obj",         (getter)DelegateProxy_wrapped,  (setter)NULL, 
      "Returns the proxy's wrapped object.", NULL },
      
    { NULL }
};

/**
 * Type declaration and instance.
 */
static PyTypeObject DelegateProxyType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name        = "hexchat.DelegateProxy",
    .tp_doc         = "Wraps an object and provides Delegates for its methods.",
    .tp_basicsize   = sizeof(DelegateProxyObj),
    .tp_itemsize    = 0,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_new         = PyType_GenericNew,
    .tp_init        = (initproc)DelegateProxy_init,
    .tp_dealloc     = (destructor)DelegateProxy_dealloc,
    .tp_members     = DelegateProxy_members,
    .tp_methods     = DelegateProxy_methods,
    .tp_getset      = DelegateProxy_accessors,
    .tp_richcompare = (richcmpfunc)DelegateProxy_cmp,
    .tp_repr        = (reprfunc)DelegateProxy_repr,
    .tp_hash        = (hashfunc)DelegateProxy_hash,
    .tp_getattro    = (getattrofunc)DelegateProxy_getattro,  
};

/**
 * Convenience pointer to the type.
 */
PyTypeObject *DelegateProxyTypePtr = &DelegateProxyType;

/**
 * Constructor.
 * @param self      - Object instance.
 * @param args      - From python there are two positional arguments:
 *                    the wrapped object and 'is_async'. If 'is_async' is True
 *                    the DelegateProxy will provide asynchronous delegates to
 *                    the wrapped objects methods. If False, the delegates
 *                    are synchronous.
 * @param kwargs    - not used.
 * @returns - 0 on success, or -1 on failure with error state set.
 */
int
DelegateProxy_init(DelegateProxyObj *self, PyObject *args, PyObject *kwargs)
{
    PyObject *pywrapped;
    int      is_async = false;

    if (!PyArg_ParseTuple(args, "O|p:__init__",  &pywrapped, &is_async)) {
        return -1;
    }
    self->obj         = pywrapped;
    self->is_async    = is_async;
    self->cache       = PyDict_New();

    Py_INCREF(pywrapped);

    return 0;
}

void
DelegateProxy_dealloc(DelegateProxyObj *self)
{
    Py_DECREF(self->cache);
    Py_DECREF(self->obj);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/**
 * Returns attributes of the wrapped object. If the attribute is callable, it 
 * is wrapped in a Delegate object which will execute it on the main thread 
 * when invoked. A cache of Delegates is maintained so subsequent accesses of
 * a callable attribute returns the same Delegate.
 * @param self      - instance.
 * @param pyname    - The name of the attribute being accessed.
 * @returns - The attribute, or a Delegate for callable attributes. NULL on
 *            failure with error state set.
 */
PyObject *
DelegateProxy_getattro(DelegateProxyObj *self, PyObject *pyname)
{
    PyObject *pyretval;
    PyObject *pyattr;

    // See if the requested attribute is defined elsewhere first.
    if ((pyretval = PyObject_GenericGetAttr((PyObject *)self, pyname))) {
        return pyretval;
    }
    // An AttributeError is expected if the attribute isn't one of the 
    // generics.

    // If the error is an AttributeError, dismiss it; otherwise, return NULL 
    // to propagate the error.
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) {
        return NULL;
    }
    PyErr_Clear();

    // Get the attribute from the wrapped object.
    pyattr   = PyObject_GetAttr(self->obj, pyname); // NR.
    pyretval = pyattr;

    if (!pyattr) {
        return NULL;
    }
    
    if (PyDict_Contains(self->cache, pyattr)) {
        pyretval = PyDict_GetItem(self->cache, pyattr); // BR.
        Py_INCREF(pyretval);
        Py_DECREF(pyattr);
    }
    else if (PyCallable_Check(pyattr)) {
        pyretval = PyObject_CallFunction((PyObject *)DelegateTypePtr,
                                         "Oi", pyattr, self->is_async);
        PyDict_SetItem(self->cache, pyattr, pyretval);
        Py_DECREF(pyattr);
    }
    return pyretval;
}

/**
 * Accessor for 'is_async'.
 */
PyObject *
DelegateProxy_is_async(DelegateProxyObj *self, void *closure)
{
    if (self->is_async) {
        Py_RETURN_TRUE;
    }
    else {
        Py_RETURN_FALSE;
    }
}

/**
 * 'obj' getter for the wrapped object.
 */
PyObject *
DelegateProxy_wrapped(DelegateProxyObj *self, void *closure)
{
    Py_INCREF(self->obj);
    return self->obj;
}

/**
 * Returns a list of the class/object attributes. This is called when dir(o)
 * is invoked on the ListIter class or an object.
 */
PyObject *
DelegateProxy_dir(DelegateProxyObj *self)
{
    PyObject    *pydir_list1;
    PyObject    *pydir_list2;
    PyObject    *pydir_list3;
    PyObject    *pyset;

    pydir_list1 = PyObject_Dir((PyObject *)Py_TYPE(self));
    pydir_list2 = PyObject_Dir(self->obj);
    pydir_list3 = PySequence_InPlaceConcat(pydir_list1, pydir_list2);

    pyset = PySet_New(pydir_list3);
    
    Py_XDECREF(pydir_list1);
    Py_XDECREF(pydir_list2);
    Py_XDECREF(pydir_list3);

    if (PyErr_Occurred()) {
        // This should not happen.
        Py_XDECREF(pyset);
        return NULL;
    }

    return pyset;
}

/**
 * Returns an intuitive representation of the DelegateProxy instance.
 */
PyObject *
DelegateProxy_repr(DelegateProxyObj *self, PyObject *Py_UNUSED(args))
{
    PyObject *pyrepr;
    PyObject *pyobjrepr;
    PyObject *pyasync;
    
    pyobjrepr = PyObject_Repr(self->obj);
    pyasync   = (self->is_async) ? Py_True : Py_False;

    pyrepr = PyUnicode_FromFormat("DelegateProxy(%U, is_async=%R)", 
                                  pyobjrepr, 
                                  pyasync);
    
    Py_DECREF(pyobjrepr);
    
    return pyrepr;
}

/**
 * Implements the comparison function. DelegateProxy instances are equal to
 * other DelegateProxy instances that wrap objects that are equal.
 */
PyObject *
DelegateProxy_cmp(DelegateProxyObj *a, PyObject *b, int op)
{
    DelegateProxyObj *delb = (DelegateProxyObj *)b;

    if (Py_TYPE(b) == DelegateProxyTypePtr) {

        return PyObject_RichCompare(a->obj, delb->obj, op);
    }
    Py_RETURN_FALSE;
}

/**
 * Returns a hash for the instance based on the wrapped object's hash, but not
 * the same value.
 */
Py_hash_t
DelegateProxy_hash(DelegateProxyObj *self)
{
    Py_hash_t hash1, hash2;
    
    hash1 = PyObject_Hash(self->obj);
    hash2 = PyObject_Hash((PyObject *)Py_TYPE(self));
    
    return hash1 + hash2;
}

