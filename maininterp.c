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
 * This file provides access to the main Python interpreter environment. The
 * primary intent of the interface is to allow the loading of Python modules
 * that have trouble being loaded into a subinterpreter. A plugin running in
 * a subinterpreter can have the main intepreter load modules, then access
 * them via the MainInterp singleton instance.
 *
 * The MainInterpObj instance implements a custom tp_getattro function (Think
 * __getattr__() in Python) to provide access to the root __main__ namespace.
 * Any MainInterp callable attribute invoked from a subinterpreter causes
 * the call to be delegated to the main interpreter on the HexChat main thread.
 */

#include "minpython.h"

/**
 * Instance data.
 */
typedef struct {
    PyObject_HEAD
    PyThreadState *threadstate;
} MainInterpObj;

/**
 * MainInterp singleton instance.
 */
MainInterpObj *main_interp = NULL;

static PyObject *MainInterp_new      (PyTypeObject *, PyObject *, PyObject *);
static int      MainInterp_init      (MainInterpObj *, PyObject *, PyObject *);
static void     MainInterp_dealloc   (MainInterpObj *);
static PyObject *MainInterp_getattro (MainInterpObj *, PyObject *);


/**
 * Accessors.
 */
/*
static PyGetSetDef MainInterp_accessors[] = {
  { "server_time_utc",   (getter)MainInterp_get_time,
                         (setter)MainInterp_set_time,
    "Server time.", NULL },
    { NULL }
};
*/

/**
 * Type declaration/instance.
 */
static PyTypeObject MainInterpType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name        = "hexchat.MainInterp",
    .tp_doc         = "Provides access to the main Python interpreter.",
    .tp_basicsize   = sizeof(MainInterpObj),
    .tp_itemsize    = 0,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_new         = MainInterp_new,
    .tp_init        = (initproc)MainInterp_init,
    .tp_dealloc     = (destructor)MainInterp_dealloc,
    //.tp_getset      = MainInterp_accessors,
    .tp_getattro    = (getattrofunc)MainInterp_getattro,
};

/**
 * Convenient type pointer.
 */
PyTypeObject *MainInterpTypePtr = &MainInterpType;

PyObject *
MainInterp_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    if (!main_interp) {
        main_interp = (MainInterpObj *)type->tp_alloc(type, 0);
        if (main_interp) {
            main_interp->threadstate = py_g_main_threadstate;
        }
        else {
            return NULL;
        }
    }
    return (PyObject *)main_interp;
}

int
MainInterp_init(MainInterpObj *self, PyObject *args, PyObject *kwargs)
{
    if (!PyArg_ParseTuple(args, "")) {
        return -1;
    }
    return 0;
}

void
MainInterp_dealloc(MainInterpObj *self)
{
    Py_TYPE(self)->tp_free((PyObject *) self);
}


PyObject *
MainInterp_getattro(MainInterpObj *self, PyObject *pyname)
{
    PyObject *pyretval;
    PyObject *pydir;

    // See if the requested attribute is defined elsewhere first.
    if ((pyretval = PyObject_GenericGetAttr((PyObject *)self, pyname))) {
        return pyretval;
    }
    // An AttributeError is expected if the attribute isn't one of the generics.

    // If the error is an AttributeError, dismiss it; otherwise, return NULL to
    // propagate the error.
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) {
        return NULL;
    }
    PyErr_Clear();

    // Need to switch to the main interp threadstate and check the root
    // dict for the attribute being requested.

    Py_RETURN_NONE;
}


