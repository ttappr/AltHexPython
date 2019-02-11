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
 * InterpCall objects execute callables in a specific (sub)interprer's
 * environment.
 */

#include "minpython.h"

/**
 * Instance data.
 */
typedef struct {
    PyObject_HEAD
    PyObject      *callable;
    PyObject      *tscap;
    PyThreadState *threadstate;
} InterpCallObj;


static int      InterpCall_init      (InterpCallObj *, PyObject *, PyObject *);
static void     InterpCall_dealloc   (InterpCallObj *);
static PyObject *InterpCall_call     (InterpCallObj *, PyObject *, PyObject *);
static PyObject *InterpCall_repr     (InterpCallObj *, PyObject *);


/**
 * Type declaration/instance.
 */
static PyTypeObject InterpCallType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name        = "hexchat.InterpCall",
    .tp_doc         = "Wraps a callable and invokes it in another interp.",
    .tp_basicsize   = sizeof(InterpCallObj),
    .tp_itemsize    = 0,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_new         = PyType_GenericNew,
    .tp_init        = (initproc)InterpCall_init,
    .tp_dealloc     = (destructor)InterpCall_dealloc,
    .tp_call        = (ternaryfunc)InterpCall_call,
    .tp_repr        = (reprfunc)InterpCall_repr,
};

/**
 * Convenient type pointer.
 */
PyTypeObject *InterpCallTypePtr = &InterpCallType;


int
InterpCall_init(InterpCallObj *self, PyObject *args, PyObject *kwargs)
{
    PyObject    *pyinterp   = NULL;
    PyObject    *pyfunc;
    static char *keywords[] = { "func", "interp", NULL };

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|O:__init__", keywords,
                                     &pyfunc, &pyinterp)) {
        return -1;
    }
    if (!PyCallable_Check(pyfunc)) {
        PyErr_SetString(PyExc_TypeError,
                        "InterpCall constructor requires a callable object "
                        "for 'func' parameter.");
        return -1;
    }
    if (pyinterp) {
        if (!PyCapsule_CheckExact(pyinterp) ||
                strcmp(PyCapsule_GetName(pyinterp), "interp")) {

            PyErr_SetString(PyExc_TypeError,
                            "InterpCall constructor requires an interp "
                            "capsule for 'interp' parameter.");
            return -1;
        }
        Py_INCREF(pyinterp);
    }
    else {
        pyinterp = PyCapsule_New(PyThreadState_Get(), "interp", NULL);
    }
    self->tscap       = pyinterp;
    self->threadstate = PyCapsule_GetPointer(pyinterp, "interp");
    self->callable    = pyfunc;

    Py_INCREF(pyfunc);

    return 0;
}

void
InterpCall_dealloc(InterpCallObj *self)
{
    Py_XDECREF(self->callable);
    Py_XDECREF(self->tscap);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


PyObject *
InterpCall_call(InterpCallObj *self, PyObject *args, PyObject *kwargs)
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
        if (PyObject_IsInstance(pyobj, (PyObject *)InterpCallTypePtr)     ||
            PyObject_IsInstance(pyobj, (PyObject *)InterpObjProxyTypePtr) ||
            interp_is_primitive(pyobj)) {
            Py_INCREF(pyobj);
        }
        else if (PyCallable_Check(pyobj)) {
            pyobj = PyObject_CallFunction((PyObject *)InterpCallTypePtr,
                                          "O", pyobj);
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

            if (PyObject_IsInstance(pyobj, (PyObject *)InterpCallTypePtr)     ||
                PyObject_IsInstance(pyobj, (PyObject *)InterpObjProxyTypePtr) ||
                interp_is_primitive(pyobj)) {
                Py_INCREF(pyobj);
            }
            else if (PyCallable_Check(pyobj)) {
                pyobj = PyObject_CallFunction((PyObject *)InterpCallTypePtr,
                                              "O", pyobj);
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
    pyret = PyObject_Call(self->callable, pytup, pydict);

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
    else if (PyCallable_Check(pyret)) {
        pytmp = PyObject_CallFunction((PyObject *)InterpCallTypePtr,
                                      "OO", pyret, self->tscap);
        Py_DECREF(pyret);
        pyret = pytmp;
    }
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

PyObject *
InterpCall_repr(InterpCallObj *self, PyObject *Py_UNUSED(args))
{
    PyObject *pyrepr;
    PyObject *pyobjrepr;

    pyobjrepr = PyObject_Repr(self->callable);

    pyrepr = PyUnicode_FromFormat("InterpCall(%U)", pyobjrepr);

    Py_DECREF(pyobjrepr);

    return pyrepr;
}









