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
 * Handles the loading of Python plugin modules. Features include blanking
 * out a loaded module to simulate /UNLOAD of the module by reloading the
 * module with an empty script.
 */

// TODO - Insert the custom loader/finder into the import system:
//        sys.meta_path.insert(0, MyMetaFinder())
//      - To allocate for the new objects, use PyMem_(Raw)Realloc() and handle
//        the subclass data area as an offset into the reallocated block.
//        During module initialization capture the offset in a static variable.
//        Also might have to define these types using PEP0384
//        - PyType_FromSpec(WithBases)()
//      - Get the tp_basicsize from the superclass while dynamically defining
//        the new type.

#include "minpython.h"

typedef struct {
    PyObject_HEAD
} MetaFinderObj;

typedef struct {
    PyObject_HEAD
} LoaderObj;

       int      loader_initialize     (PyObject *);

static int      MetaFinder_init       (MetaFinderObj *, PyObject *, PyObject *);
static void     MetaFinder_dealloc    (MetaFinderObj *);
static PyObject *MetaFinder_find_spec (MetaFinderObj *, PyObject *, PyObject *);

static int      Loader_init           (LoaderObj *, PyObject *, PyObject *);
static void     Loader_dealloc        (LoaderObj *);
static PyObject *Loader_create_module (LoaderObj *, PyObject *);
static PyObject *Loader_exec_module   (LoaderObj *, PyObject *);

static PyMethodDef MetaFinder_methods[] = {
    {"find_spec",       (PyCFunction)MetaFinder_find_spec,  METH_VARARGS |
                                                            METH_KEYWORDS,
     "Finds the 'spec' for a plugin module.", NULL},
    {NULL}
};

static PyMethodDef Loader_methods[] = {
    {"create_module",   (PyCFunction)Loader_create_module,  METH_VARARGS,
     "Just returns None so the default creation algorithm is used.", NULL},
    {"exec_module",     (PyCFunction)Loader_exec_module,    METH_VARARGS,
     "Executes the plugin script and performs additional setup.", NULL},
    {NULL}
};

static PyTypeObject MetaFinder = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name        = "hexchat.MetaFinder",
    .tp_doc         = "Finder for Python HexChat plugins",
    .tp_basicsize   = sizeof(MetaFinderObj),
    .tp_itemsize    = 0,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    //.tp_base      = importlib.abc.MetaPathFinder,
    .tp_init        = (initproc)MetaFinder_init,
    .tp_dealloc     = (destructor)MetaFinder_dealloc,
    .tp_methods     = MetaFinder_methods,
};

static PyTypeObject Loader = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name        = "hexchat.Loader",
    .tp_doc         = "Loader for Python HexChat plugins",
    .tp_basicsize   = sizeof(LoaderObj),
    .tp_itemsize    = 0,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    //.tp_base      = inportlib.abc.Loader,
    .tp_init        = (initproc)Loader_init,
    .tp_dealloc     = (destructor)Loader_dealloc,
    .tp_methods     = Loader_methods,
};

/**
 * Initializes the MetaFinder and Loader types for the plugin loader module.
 * This is called from within the hexchat module initialization routine.
 */
int
loader_initialize(PyObject *hcmodule)
{
    PyObject *pyimplib      = NULL;
    PyObject *pybasefinder  = NULL;
    PyObject *pybaseloader  = NULL;
    int      retval         = 0;

    // Grab the base classes to inherit from.
    if (!(pyimplib  = PyImport_Import("importlib.abc")) {
        goto error;
    }
    if (!(pybasefinder = PyObject_GetAttrString(pyimplib, "MetaPathFinder")) {
        goto error;
    }
    if (!(pybaseloader = PyObject_GetAttrString(pyimplib, "Loader")) {
        goto error;
    }

    // Set up inheritance for the types.
    MetaFinder.tp_base  = (PyTypeObject *)pybasefinder;
    Loader.tp_base      = (PyTypeObject *)pybaseloader;

    // Finish initializing the new types.
    if (!PyType_Ready(&MetaFinder)) {
        goto error;
    }
    if (!PyType_Ready(&Loader)) {
        goto error;
    }

    // Add new types to the hexchat module.
    PyModule_AddObject(hcmodule, "_MetaFinder", &MetaFinder);
    PyModule_AddObject(hcmodule, "_Loader", &Loader);

    goto finally;

error:
    ;
    Py_XDECREF(pybasefinder);
    Py_XDECREF(pybaseloader);
    retval = -1;

finally:
    ;
    Py_XDECREF(pyimplib);
    return retval;
}

int
MetaFinder_init(MetaFinderObj *self, PyObject *args, PyObject *kwargs)
{
    // Might not need to define init and dealloc functions since there are
    // no added variables to the objects created.
    // Try loading and using this DLL without them to see what happens.

    Py_TYPE(self)->tp_base->tp_init(self, args, kwargs);
    return 0;
}

void
MetaFinder_dealloc(MetaFinderObj *self)
{
    Py_TYPE(self)->tp_free((PyObject *)self);
}

PyObject *
MetaFinder_find_spec(MetaFinderObj *self, PyObject *args, PyObject *kwargs)
{
    Py_RETURN_NONE;
}

int
Loader_init(LoaderObj *self, PyObject *args, PyObject *kwargs)
{
    return 0;
}

void
Loader_dealloc(LoaderObj *self)
{

}

PyObject *
Loader_create_module(LoaderObj *self, PyObject *args)
{
    Py_RETURN_NONE;
}

PyObject *
Loader_exec_module(LoaderObj *self, PyObject *args)
{
    Py_RETURN_NONE;
}
