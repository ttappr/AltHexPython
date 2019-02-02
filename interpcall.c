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
 *
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
    .tp_init        = (initproc)InterpCall_init,
    .tp_dealloc     = (destructor)InterpCall_dealloc,
    .tp_call        = (ternaryfunc)InterpCall_call,
};

/**
 * Convenient type pointer.
 */
PyTypeObject *InterpCallTypePtr = &InterpCallType;


int
InterpCall_init(InterpCallObj *self, PyObject *args, PyObject *kwargs)
{
    PyObject    *pyinterp;
    PyObject    *pyfunc;
    static char *keywords[] = { "interp", "func", NULL };

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO", keywords,
                                     &pyinterp, &pyfunc)) {
        return -1;
    }
    if (!PyCallable_Check(pyfunc)) {
        PyErr_SetString(PyExc_TypeError,
                        "InterpCall constructor requires a callable object "
                        "for 'func' parameter.");
        return -1;
    }
    if (!PyCapsule_CheckExact(pyinterp) ||
            strcmp(PyCapsule_GetName(pyinterp), "interp")) {

        PyErr_SetString(PyExc_TypeError,
                        "InterpCall constructor requires an interp capsule "
                        "for 'interp' parameter.");
        return -1;
    }
    self->tscap       = pyinterp;
    self->threadstate = PyCapsule_GetPointer(pyinterp, "interp");
    self->callable    = pyfunc;

    Py_INCREF(pyinterp);
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
    PyObject     *pyexc_type;
    PyObject     *pyexc;
    PyObject     *pytraceback;
    SwitchTSInfo tsinfo;

    // Switch to target interpreter.
    switch_threadstate(self->threadstate);

    // Invoke call.
    pyret = PyObject_Call(self->callable, args, kwargs);

    if (!pyret) {
        // If error, capture exception data, clearing it in the target interp.
        PyErr_Fetch(&pyexc_type, &pyexc, &pytraceback);
        PyErr_NormalizeException(&pyexc_type, &pyexc, &pytraceback);
        // PyErr_SetTraceback(pyexc, pytraceback);
    }

    // Switch back to caller.
    switch_threadstate_back(tsinfo);

    if (!pyret) {
        // If error, restore the exception in the calling interp.
        PyErr_Restore(pyexc_type, pyexc, pytraceback);
    }

    return pyret;
}











