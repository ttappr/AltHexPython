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
 * A InterpObjProxy wraps objects and provides Delegates for their methods
 * or functions. There are two ready-made InterpObjProxy's provided by the
 * hexchat module: 'hexchat.synchronous' and 'hexchat.asynchronous'. Each
 * provides Delegates for the hexchat API. The synchronous InterpObjProxy
 * provides synchronous Delegates for the API, and the asynchronous
 * InterpObjProxy provides asynchronous Delegates. InterpObjProxy's can be
 * created to wrap any arbitrary object if desired.
 */

/**
 * TODO - Add accessors for wrapped object. For example win.title = 'foo'
 *        should work transparently. Use tp_setattro to do this.
 *      - Can use PyObject_IsInstance() to see if self->obj is a type, then
 *        maybe handle handle differently in .tp_call().
 */

#include "minpython.h"

/**
 * Instance object data.
 */
typedef struct {
    PyObject_HEAD
    PyObject      *obj;
    PyObject      *cache;
    PyObject      *tscap;
    PyThreadState *threadstate;
} InterpObjProxyObj;

static int      InterpObjProxy_init      (InterpObjProxyObj *, PyObject *,
                                                               PyObject *);
static void     InterpObjProxy_dealloc   (InterpObjProxyObj *);
static PyObject *InterpObjProxy_getattro (InterpObjProxyObj *, PyObject *);
static int      InterpObjProxy_setattro  (InterpObjProxyObj *, PyObject *,
                                                               PyObject *);
static PyObject *InterpObjProxy_dir      (InterpObjProxyObj *);
static PyObject *InterpObjProxy_wrapped  (InterpObjProxyObj *, void *);
static Py_hash_t InterpObjProxy_hash     (InterpObjProxyObj *);
static PyObject *InterpObjProxy_cmp      (InterpObjProxyObj *, PyObject *, int);
static PyObject *InterpObjProxy_repr     (InterpObjProxyObj *, PyObject *);
static PyObject *InterpObjProxy_call     (InterpObjProxyObj *, PyObject *,
                                                               PyObject *);


/**
 * InterpObjProxy instance members.
 */
static PyMemberDef InterpObjProxy_members[] = {
    { NULL }
};

/**
 * InterpObjProxy methods.
 */
static PyMethodDef InterpObjProxy_methods[] = {
    {"__dir__",   (PyCFunction)InterpObjProxy_dir,      METH_NOARGS,
     "Returns attributes of InterpObjProxy, which include the attributes of the "
     "wrapped object."},

    { NULL }
};

/**
 * Instance getters and setters.
 */
static PyGetSetDef InterpObjProxy_accessors[] = {
    { "obj",         (getter)InterpObjProxy_wrapped,  (setter)NULL,
      "Returns the proxy's wrapped object.", NULL },

    { NULL }
};

/**
 * Type declaration and instance.
 */
static PyTypeObject InterpObjProxyType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name        = "hexchat.InterpObjProxy",
    .tp_doc         = "Wraps an object from a specific interp and proxies "
                      "attribute accesses to it.",
    .tp_basicsize   = sizeof(InterpObjProxyObj),
    .tp_itemsize    = 0,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_new         = PyType_GenericNew,
    .tp_init        = (initproc)InterpObjProxy_init,
    .tp_dealloc     = (destructor)InterpObjProxy_dealloc,
    .tp_members     = InterpObjProxy_members,
    .tp_methods     = InterpObjProxy_methods,
    .tp_getset      = InterpObjProxy_accessors,
    .tp_richcompare = (richcmpfunc)InterpObjProxy_cmp,
    .tp_repr        = (reprfunc)InterpObjProxy_repr,
    .tp_hash        = (hashfunc)InterpObjProxy_hash,
    .tp_getattro    = (getattrofunc)InterpObjProxy_getattro,
    .tp_setattro    = (setattrofunc)InterpObjProxy_setattro,
    .tp_call        = (ternaryfunc)InterpObjProxy_call,
};

/**
 * Convenience pointer to the type.
 */
PyTypeObject *InterpObjProxyTypePtr = &InterpObjProxyType;

/**
 * Constructor.
 * @param self      - Object instance.
 * @param args      - From python there are two positional arguments:
 *                    the wrapped object and 'is_async'. If 'is_async' is True
 *                    the InterpObjProxy will provide asynchronous delegates to
 *                    the wrapped objects methods. If False, the delegates
 *                    are synchronous.
 * @param kwargs    - not used.
 * @returns - 0 on success, or -1 on failure with error state set.
 */
int
InterpObjProxy_init(InterpObjProxyObj *self, PyObject *args, PyObject *kwargs)
{
    PyObject *pyinterp = NULL;
    PyObject *pyobj;

    static char *keywords[] = { "obj", "interp", NULL };

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|O:__init__", keywords,
                                     &pyobj, &pyinterp)) {
        return -1;
    }
    if (pyinterp) {
        if (!PyCapsule_CheckExact(pyinterp) ||
                strcmp(PyCapsule_GetName(pyinterp), "interp")) {

            PyErr_SetString(PyExc_TypeError,
                            "InterpObjProxy constructor requires an interp "
                            "capsule for 'interp' parameter.");
            return -1;
        }
    }
    else {
        pyinterp = PyCapsule_New(PyThreadState_Get(), "interp", NULL);
    }
    self->tscap       = pyinterp;
    self->threadstate = PyCapsule_GetPointer(pyinterp, "interp");
    self->obj         = pyobj;
    self->cache       = PyDict_New();

    Py_INCREF(pyinterp);
    Py_INCREF(pyobj);

    return 0;
}

void
InterpObjProxy_dealloc(InterpObjProxyObj *self)
{
    Py_XDECREF(self->cache);
    Py_XDECREF(self->obj);
    Py_XDECREF(self->tscap);
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
InterpObjProxy_getattro(InterpObjProxyObj *self, PyObject *pyname)
{
    PyObject     *pyretval;
    PyObject     *pyattr;
    PyObject     *pyexc_type    = NULL;
    PyObject     *pyexc         = NULL;
    PyObject     *pytraceback   = NULL;
    Py_hash_t    hash;
    PyObject     *pyhashobj;
    SwitchTSInfo tsinfo;

    // TODO - Need to figure out what to do with types returned by the
    //        wrapped object, can they safely be returned without wrapping?
    //        Or can a subinterp subclass the proxy whose object is a type?

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

    tsinfo = switch_threadstate(self->threadstate);

    // Get the attribute from the wrapped object.
    pyattr   = PyObject_GetAttr(self->obj, pyname); // NR.
    
    if (PyErr_Occurred()) {
        // If error, capture exception data, clearing it in the target interp.
        PyErr_Fetch(&pyexc_type, &pyexc, &pytraceback);
        PyErr_NormalizeException(&pyexc_type, &pyexc, &pytraceback);
        if (pyattr) {
            Py_DECREF(pyattr);
            pyattr = NULL;
        }
    }
    else {
        hash = PyObject_Hash(pyattr);
        if (PyErr_Occurred()) {
            PyErr_Clear();
        }
    }

    switch_threadstate_back(tsinfo);
    
    if(!pyattr) {
        // If error, restore the exception in the calling interp.
        PyErr_Restore(pyexc_type, pyexc, pytraceback);
        return NULL;
    }

    if (hash == -1) {
        pyhashobj = PyLong_FromVoidPtr(pyattr);
    }
    else {
        pyhashobj = pyattr;
        Py_INCREF(pyhashobj);
    }

    // TODO - Need to determine which objects to wrap in a proxy before
    //        returning. Don't want to wrap basic types like int, str, etc.
    if (interp_is_primitive(pyattr)) {
        pyretval = pyattr;
        Py_INCREF(pyretval);
    }
    else if (PyDict_Contains(self->cache, pyhashobj)) {
        pyretval = PyDict_GetItem(self->cache, pyhashobj); // BR.
        Py_INCREF(pyretval);
    }
    /*
    else if (PyCallable_Check(pyattr)) {
        pyretval = PyObject_CallFunction((PyObject *)InterpCallTypePtr,
                                         "OO", pyattr, self->tscap);
        PyDict_SetItem(self->cache, pyhashobj, pyretval);
    }
    */
    else {
        pyretval = PyObject_CallFunction((PyObject *)InterpObjProxyTypePtr,
                                         "OO", pyattr, self->tscap);
        PyDict_SetItem(self->cache, pyhashobj, pyretval);
    }
    Py_DECREF(pyattr);
    Py_DECREF(pyhashobj);

    return pyretval;
}

int
InterpObjProxy_setattro(InterpObjProxyObj *self, PyObject *name,
                                                 PyObject *value)
{
    PyObject     *pyexc_type    = NULL;
    PyObject     *pyexc         = NULL;
    PyObject     *pytraceback   = NULL;
    int          ret;
    SwitchTSInfo tsinfo;

    ret = PyObject_GenericSetAttr((PyObject *)self, name, value);

    if (ret == -1) {
        PyErr_Clear();

        // TODO - Wrap nonprimitives in proxy.

        tsinfo = switch_threadstate(self->threadstate);

        ret = PyObject_SetAttr(self->obj, name, value);

        if (ret == -1) {
            PyErr_Fetch(&pyexc_type, &pyexc, &pytraceback);
            PyErr_NormalizeException(&pyexc_type, &pyexc, &pytraceback);
        }

        switch_threadstate_back(tsinfo);

        if (ret == -1) {
            PyErr_Restore(pyexc_type, pyexc, pytraceback);
        }

    }
    return ret;
}

/**
 * 'obj' getter for the wrapped object.
 */
PyObject *
InterpObjProxy_wrapped(InterpObjProxyObj *self, void *closure)
{
    Py_INCREF(self->obj);
    return self->obj;
}

/**
 * Returns a list of the class/object attributes. This is called when dir(o)
 * is invoked on the ListIter class or an object.
 */
PyObject *
InterpObjProxy_dir(InterpObjProxyObj *self)
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
 * Returns an intuitive representation of the InterpObjProxy instance.
 */
PyObject *
InterpObjProxy_repr(InterpObjProxyObj *self, PyObject *Py_UNUSED(args))
{
    PyObject *pyrepr;
    PyObject *pyobjrepr;

    pyobjrepr = PyObject_Repr(self->obj);

    pyrepr = PyUnicode_FromFormat("InterpObjProxy(%U)", pyobjrepr);

    Py_DECREF(pyobjrepr);

    return pyrepr;
}

/**
 * Implements the comparison function. InterpObjProxy instances are equal to
 * other InterpObjProxy instances that wrap objects that are equal.
 */
PyObject *
InterpObjProxy_cmp(InterpObjProxyObj *a, PyObject *b, int op)
{
    InterpObjProxyObj *delb = (InterpObjProxyObj *)b;

    if (Py_TYPE(b) == InterpObjProxyTypePtr) {

        return PyObject_RichCompare(a->obj, delb->obj, op);
    }
    Py_RETURN_FALSE;
}

/**
 * Returns a hash for the instance based on the wrapped object's hash, but not
 * the same value.
 */
Py_hash_t
InterpObjProxy_hash(InterpObjProxyObj *self)
{
    Py_hash_t hash1, hash2;

    hash1 = PyObject_Hash(self->obj);
    hash2 = PyObject_Hash((PyObject *)Py_TYPE(self));

    return hash1 + hash2;
}

PyObject *
InterpObjProxy_call(InterpObjProxyObj *self, PyObject *args, PyObject *kwargs)
{
    PyObject     *pyret;
    PyObject     *pytmp;
    PyObject     *pyexc_type    = NULL;
    PyObject     *pyexc         = NULL;
    PyObject     *pytraceback   = NULL;
    PyObject     *pytup;
    PyObject     *pydict        = NULL;
    PyObject     *pykey;
    PyObject     *pyobj;
    Py_ssize_t   i, j, len;
    SwitchTSInfo tsinfo;

    // Wrap callables and non basic types in a proxy that executes in the
    // caller's context.
    len    = PyObject_Length(args);
    pytup  = PyTuple_New(len);

    for (i = 0; i < len; i++) {
        pyobj = PyTuple_GetItem(args, i); // BR.
        if (PyObject_IsInstance(pyobj, (PyObject *)InterpObjProxyTypePtr) ||
            interp_is_primitive(pyobj)) {
            Py_INCREF(pyobj);
        }
        else {
            pyobj = PyObject_CallFunction((PyObject *)InterpObjProxyTypePtr,
                                          "O", pyobj);
        }
        PyTuple_SetItem(pytup, i, pyobj);
    }
    if (kwargs) {
        len    = PyDict_Size(kwargs);
        pydict = PyDict_New();
        for (i = 0; i < len; i++) {
            PyDict_Next(kwargs, &j, &pykey, &pyobj);

            if (PyObject_IsInstance(pyobj, (PyObject *)InterpObjProxyTypePtr) ||
                interp_is_primitive(pyobj)) {
                Py_INCREF(pyobj);
            }
            else {
                pyobj = PyObject_CallFunction((PyObject *)InterpObjProxyTypePtr,
                                              "O", pyobj);
            }
            PyDict_SetItem(pydict, pykey, pyobj);
            Py_DECREF(pyobj);
        }
    }

    // Switch to target interpreter.
    tsinfo = switch_threadstate(self->threadstate);

    // Invoke call.
    pyret = PyObject_Call(self->obj, pytup, pydict);

    if (!pyret) {
        // If error, capture exception data, clearing it in the target interp.
        PyErr_Fetch(&pyexc_type, &pyexc, &pytraceback);
        PyErr_NormalizeException(&pyexc_type, &pyexc, &pytraceback);
        // PyErr_SetTraceback(pyexc, pytraceback);
    }

    // Switch back to caller.
    switch_threadstate_back(tsinfo);

    Py_DECREF(pytup);
    Py_XDECREF(pydict);

    if (!pyret) {
        // If error, restore the exception in the calling interp.
        PyErr_Restore(pyexc_type, pyexc, pytraceback);
    }
    else if (pyret == Py_None) {
        // Do Nothing.
    }
    /*
    else if (PyCallable_Check(pyret)) {
        pytmp = PyObject_CallFunction((PyObject *)InterpCallTypePtr,
                                      "OO", pyret, self->tscap);
        Py_DECREF(pyret);
        pyret = pytmp;
    }
    */
    else {
        if (!interp_is_primitive(pyret)) {
            pytmp = PyObject_CallFunction((PyObject *)InterpObjProxyTypePtr,
                                          "OO", pyret, self->tscap);
            Py_DECREF(pyret);
            pyret = pytmp;
        }
    }

    return pyret;
}



