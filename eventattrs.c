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

typedef struct {
    PyObject_HEAD
    PyObject *server_time_utc;
} EventAttrsObj;

static int      EventAttrs_init        (EventAttrsObj *, PyObject *, PyObject *);
static void     EventAttrs_dealloc     (EventAttrsObj *);
static PyObject *EventAttrs_get_time    (EventAttrsObj *, void *);
static int      EventAttrs_set_time     (EventAttrsObj *, PyObject *, void *);

static PyObject *EventAttrs_cmp        (EventAttrsObj *, PyObject *, int);
static PyObject *EventAttrs_repr       (EventAttrsObj *, PyObject *);
/*
static PyMemberDef EventAttrs_members[] = {
    {"server_time_utc", T_OBJECT_EX, offsetof(EventAttrsObj, server_time_utc), 
    0, 
    "Server time value - internally converted to time_t C type on API calls."}, 
    {NULL}
};
*/
/*
static PyMethodDef EventAttrs_methods[] = {
    {"write",   (PyCFunction)EventAttrs_write,      METH_VARARGS | METH_KEYWORDS,
     "Appends text to an internal list - calls flush() if last character is "
     "'\\n'."},
    {NULL}
};
*/

static PyGetSetDef EventAttrs_accessors[] = {
  { "server_time_utc",   (getter)EventAttrs_get_time,     
                         (setter)EventAttrs_set_time,
    "Server time.", NULL },
   /*
    { "_capsule",          (getter)EventAttrs_get_capsule, 
                           (setter)NULL,
      "The attributes capsule - for internal use.", NULL },*/

    { NULL }
};

static PyTypeObject EventAttrsType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name        = "hexchat.EventAttrs",
    .tp_doc         = "Server event attributes.",
    .tp_basicsize   = sizeof(EventAttrsObj),
    .tp_itemsize    = 0,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_new         = PyType_GenericNew,
    .tp_init        = (initproc)EventAttrs_init,
    .tp_dealloc     = (destructor)EventAttrs_dealloc,
    //.tp_members     = EventAttrs_members,
    //.tp_methods     = EventAttrs_methods,
    .tp_getset      = EventAttrs_accessors,
    .tp_richcompare = (richcmpfunc)EventAttrs_cmp,
    .tp_repr        = (reprfunc)EventAttrs_repr,
};

PyTypeObject *EventAttrsTypePtr = &EventAttrsType;

int
EventAttrs_init(EventAttrsObj *self, PyObject *args, PyObject *kwargs)
{
    PyObject *pyserver_time_utc = NULL;
    PyObject *pyval;

    static char *keywords[] = { "server_time_utc", NULL };
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O:init", keywords, 
                                     &pyserver_time_utc)) {
        return -1;
    }
    if (pyserver_time_utc) {
        if (PyLong_Check(pyserver_time_utc)) {

            self->server_time_utc = pyserver_time_utc;
            
            Py_INCREF(pyserver_time_utc);
        }
        else {
            PyErr_SetString(PyExc_TypeError, 
                            "EventAttrs() - pyserver_time_utc arg requires an "
                            "integer value.");
            return -1;
        }
    }
    else {
        pyval = PyLong_FromLongLong(time(NULL));
        if (!pyval) {
            return -1;
        }
        else {
            self->server_time_utc = pyval;
        }
    }
    return 0;
}

void
EventAttrs_dealloc(EventAttrsObj *self)
{
    Py_XDECREF(self->server_time_utc);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

PyObject *
EventAttrs_get_time(EventAttrsObj *self, void *closure)
{
    Py_INCREF(self->server_time_utc);
    return self->server_time_utc;
}

int
EventAttrs_set_time(EventAttrsObj *self, PyObject *value, void *closure)
{
    if (!PyLong_Check(value)) {
        PyErr_SetString(PyExc_TypeError, 
                        "EventAttrs.server_time_utc requires an "
                        "integer value.");
        return -1;
    }
    self->server_time_utc = value;
    Py_INCREF(value);
    return 0;
}
/*
PyObject *
EventAttrs_get_capsule(EventAttrsObj *self, void *closure)
{
    Py_INCREF(self->attrs_capsule);
    return self->attrs_capsule;
}
*/

PyObject *EventAttrs_cmp(EventAttrsObj *a, PyObject *b, int op)
{
    EventAttrsObj *bea = (EventAttrsObj *)b;

    if (Py_TYPE(b) == EventAttrsTypePtr) {
        return PyObject_RichCompare(a->server_time_utc, 
                                    bea->server_time_utc, op);
    }
    Py_RETURN_FALSE;
}

PyObject *
EventAttrs_repr(EventAttrsObj *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *pyrepr;

    pyrepr = PyUnicode_FromFormat("EventAttrs(server_time_utc=%R)",
                                  self->server_time_utc);

    return pyrepr;
}
