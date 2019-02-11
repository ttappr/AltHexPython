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

#include "minpython.h"

// PyTypeObject *BrownNoddyType = (PyTypeObject *)PyType_FromSpec(&spec);
// https://pythonextensionpatterns.readthedocs.io/en/latest/super_call.html
typedef struct {
    PyObject_HEAD
    PyObject        *obj;
    PyObject        *tscap;
    PyThreadState   *threadstate;
} InterpTypeProxyObj;


       void     interp_type_proxy_init   (void);
       PyObject *create_type_proxy       (PyObject *);

static PyObject *InterpTypeProxy_new     (PyTypeObject *,       PyObject *,
                                                                PyObject *);
static int      InterpTypeProxy_init     (InterpTypeProxyObj *, PyObject *,
                                                                PyObject *);
static void     InterpTypeProxy_dealloc  (InterpTypeProxyObj *);
static PyObject *InterpTypeProxy_getattro(InterpTypeProxyObj *, PyObject *);


static PyType_Slot InterpTypeProxy_slots[] = {
    { Py_tp_new,         InterpTypeProxy_new },
    { Py_tp_init,        InterpTypeProxy_init },
    { Py_tp_dealloc,     InterpTypeProxy_dealloc },
    { Py_tp_getattro,    InterpTypeProxy_getattro },
    { 0,                 NULL }
};

static PyType_Spec InterpTypeProxy_spec = {
    .name       = "",
    .basicsize  = sizeof(InterpTypeProxyObj),
    .itemsize   = 0,
    .flags      = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .slots      = InterpTypeProxy_slots
};

void
interp_type_proxy_init()
{
    if (!type_dict) {
        type_dict = PyDict_New();
    }
}

PyObject *
create_type_proxy(PyObject *wrapped_type)
{
    PyObject *pytype;
    PyObject *pybases;

    //pytype = PyType_FromSpec(&InterpTypeProxy_spec);

    pybases = PyTuple_New(1);
    PyTupe_SetItem(pybases, 0, wrapped_type);

    // Subclassing a type from another interp may not be a good idea. Better
    // to use a decorator pattern and hold a class ref to the foreign type.
    // But then - how to pass to foreign objects subclassed types in the
    // current interp????
    pytype = PyType_FromSpecWithBases(&InterpTypeProxy_spec, pybases);

    Py_DECREF(pybases);

    // Use this after updating the type's dictionary to hold a ref to the
    // wrapped type.
    // void PyType_Modified(PyTypeObject *type)
    return pytype;
}

PyObject *
InterpTypeProxy_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{

}

int
InterpTypeProxy_init(InterpTypeProxyObj *self, PyObject *args, PyObject *kwargs)
{
    return 0;
}

void
InterpTypeProxy_dealloc(InterpTypeProxyObj *self)
{
    Py_TYPE(self)->tp_free((PyObject *)self);
}

PyObject *
InterpTypeProxy_getattro(InterpTypeProxyObj *self, PyObject *name)
{

}
