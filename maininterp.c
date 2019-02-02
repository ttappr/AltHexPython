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
    PyObject      *tscap;
    PyThreadState *threadstate;
} MainInterpObj;

/**
 * MainInterp singleton instance.
 */
MainInterpObj *main_interp = NULL;

static PyObject *MainInterp_new      (PyTypeObject *, PyObject *, PyObject *);
static int      MainInterp_init      (MainInterpObj *, PyObject *, PyObject *);
static void     MainInterp_dealloc   (MainInterpObj *);
static PyObject *MainInterp_import   (MainInterpObj *, PyObject *);
static PyObject *MainInterp_exec     (MainInterpObj *, PyObject *);
static PyObject *MainInterp_getattro (MainInterpObj *, PyObject *);

static PyMethodDef MainInterp_methods[] = {
        {"loadmodule",   (PyCFunction)MainInterp_import, METH_VARARGS,
         "Imports a module into the main interpreter environment."},
        {"exec",         (PyCFunction)MainInterp_exec,   METH_VARARGS,
         "Executes string in main interpreter environment."},
        {NULL}
};

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
    .tp_methods     = MainInterp_methods,
    //.tp_getset      = MainInterp_accessors,
    .tp_getattro    = (getattrofunc)MainInterp_getattro,
};

/**
 * Convenient type pointer.
 */
PyTypeObject *MainInterpTypePtr = &MainInterpType;

/**
 * Always returns the singleton instance of the main interp object.
 */
PyObject *
MainInterp_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    if (!main_interp) {
        // tp_alloc() sets refcount to 1.
        main_interp = (MainInterpObj *)type->tp_alloc(type, 0);
        if (main_interp) {
            main_interp->threadstate = py_g_main_threadstate;
            main_interp->tscap       = PyCapsule_New(py_g_main_threadstate,
                                                     "interp", NULL);
        }
        else {
            return NULL;
        }
    }
    return (PyObject *)main_interp;
}

/**
 * Dummy init function. Does nothing except the singleton constructor is
 * invoked with no arguments.
 */
int
MainInterp_init(MainInterpObj *self, PyObject *args, PyObject *kwargs)
{
    if (!PyArg_ParseTuple(args, "")) {
        return -1;
    }
    return 0;
}

/**
 * Destructor. Since there's a static pointer to the singleton instance with
 * refcount always greater than 1, this shouldn't get called for the duration
 * of the HexChat process.
 */
void
MainInterp_dealloc(MainInterpObj *self)
{
    Py_TYPE(self)->tp_free((PyObject *) self);
}

PyObject *
MainInterp_import   (MainInterpObj *self, PyObject *args)
{
    PyObject     *pyname;
    PyObject     *pymod;
    PyObject     *pyexc_type, *pyexc, *pytraceback;
    SwitchTSInfo tsinfo;

    if (!PyArg_ParseTuple(args, "U", &pyname)) {
        return NULL;
    }

    tsinfo = switch_threadstate(self->threadstate);

    pymod = PyImport_Import(pyname);

    if (!pymod) {
        PyErr_Fetch(&pyexc_type, &pyexc, &pytraceback);
        PyErr_NormalizeException(&pyexc_type, &pyexc, &pytraceback);
    }

    switch_threadstate_back(tsinfo);

    if (!pymod) {
        PyErr_Restore(pyexc_type, pyexc, pytraceback);
    }

    return pymod;
}

PyObject *
MainInterp_exec(MainInterpObj *self, PyObject *args)
{
    PyObject     *pyscript;
    PyObject     *pyret;
    PyObject     *pyexc_type, *pyexc, *pytraceback;
    SwitchTSInfo tsinfo;
    int          retval;

    if (!PyArg_ParseTuple(args, "U", &pyscript)) {
        return NULL;
    }

    tsinfo = switch_threadstate(self->threadstate);

    retval = PyRun_SimpleString(PyUnicode_AsUTF8(pyscript));

    if (retval) {
        PyErr_Fetch(&pyexc_type, &pyexc, &pytraceback);
        PyErr_NormalizeException(&pyexc_type, &pyexc, &pytraceback);
    }

    switch_threadstate_back(tsinfo);

    if (retval) {
        PyErr_Restore(pyexc_type, pyexc, pytraceback);
        pyret = NULL;
    }
    else {
        pyret = Py_None;
        Py_INCREF(pyret);
    }

    return pyret;
}

/**
 * Provides access to the objects within __main__ of the main interp.
 */
PyObject *
MainInterp_getattro(MainInterpObj *self, PyObject *pyname)
{
    PyObject     *pyret;
    PyObject     *pydict;
    PyObject     *pymain;
    PyObject     *pyexc_type;
    PyObject     *pyexc;
    PyObject     *pytraceback;
    int          set_name_error = 0;
    SwitchTSInfo tsinfo;

    // See if the requested attribute is defined elsewhere first.
    if ((pyret = PyObject_GenericGetAttr((PyObject *)self, pyname))) {
        return pyret;
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

    tsinfo = switch_threadstate(self->threadstate);

    pymain = PyImport_AddModule("__main__");
    pydict = PyModule_GetDict(pymain);

    pyret  = PyDict_GetItemWithError(pydict, pyname);

    if (PyErr_Occurred()) {
        // If error, capture exception data, clearing it in the target interp.
        PyErr_Fetch(&pyexc_type, &pyexc, &pytraceback);
        PyErr_NormalizeException(&pyexc_type, &pyexc, &pytraceback);
    }
    else if (!pyret) {
        set_name_error = 1;
    }
    else {
        Py_INCREF(pyret);
    }

    switch_threadstate_back(tsinfo);

    if (!pyret) {
        if (set_name_error) {
            PyErr_Format(PyExc_NameError, "name '%S' is not defined", pyname);
        }
        else {
            // If error, restore the exception in the calling interp.
            PyErr_Restore(pyexc_type, pyexc, pytraceback);
        }
    }

    return pyret;
}















