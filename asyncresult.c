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
    PyObject *queue;
    int      done;
    PyObject *result;
    PyObject *error;
} AsyncResultObj;

static int      AsyncResult_init           (AsyncResultObj *, PyObject *, 
                                                              PyObject *);
static void     AsyncResult_dealloc        (AsyncResultObj *);

static PyObject *AsyncResult_get_result    (AsyncResultObj *, void *);
static PyObject *AsyncResult_get_error     (AsyncResultObj *, void *);

       void     asyncresult_set_error      (PyObject *, PyObject *);
       void     asyncresult_set_result     (PyObject *, PyObject *);
       
       int      asyncresult_queue_get      (AsyncResultObj *);
/*
static PyMemberDef AsyncResult_members[] = {
    { "server_time_utc", T_LONGLONG, offsetof(AsyncResultObj, server_time_utc), 0,
    "." },
    { NULL }
};
*/

static PyGetSetDef AsyncResult_accessors[] = {
    {"result",      (getter)AsyncResult_get_result,    (setter)NULL,
     "This property will block until the value is available. "
     "If no exception was raised, the result of the call is returned. "
     "Otherwise, None is returned. The caller must check the `error` property "
     "to determine whether there is a valid result or not.", 
     NULL },    
     
    {"error",       (getter)AsyncResult_get_error,     (setter)NULL,
     "If an exception was raised, this will return the exception; otherwise "
     "it returns None. This property will block until the value is available. "
     "The exception's `__traceback__` property will be set.",
     NULL },

    { NULL }
};

static PyTypeObject AsyncResultType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name        = "hexchat.AsyncResult",
    .tp_doc         = "Server event attributes.",
    .tp_basicsize   = sizeof(AsyncResultObj),
    .tp_itemsize    = 0,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_new         = PyType_GenericNew,
    .tp_init        = (initproc)AsyncResult_init,
    .tp_dealloc     = (destructor)AsyncResult_dealloc,
    //.tp_members     = AsyncResult_members,
    //.tp_methods     = AsyncResult_methods,
    .tp_getset      = AsyncResult_accessors,
};

PyTypeObject *AsyncResultTypePtr = &AsyncResultType;

static int
AsyncResult_init(AsyncResultObj *self, PyObject *args, PyObject *kwargs)
{
    PyObject    *pyqueue;
    static char *keywords[] = { "queue", NULL };

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O:init", keywords,
                                     &pyqueue)) {
        return -1;
    }
    self->queue     = pyqueue;
    
    self->error     = Py_None;
    self->result    = Py_None;
    
    Py_INCREF(pyqueue);
    Py_INCREF(self->error);
    Py_INCREF(self->result);
    return 0;
}

static void
AsyncResult_dealloc(AsyncResultObj *self)
{
    Py_XDECREF(self->result);
    Py_XDECREF(self->queue);
    Py_XDECREF(self->error);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

PyObject *
AsyncResult_get_result(AsyncResultObj *self, void *closure) 
{
    if (asyncresult_queue_get(self)) {
        return NULL;
    }
    Py_INCREF(self->result);
    return self->result;
}

PyObject *
AsyncResult_get_error(AsyncResultObj *self, void *closure)
{
    if (asyncresult_queue_get(self)) {
        return NULL;
    }
    Py_INCREF(self->error);
    return self->error;
}

int 
asyncresult_queue_get(AsyncResultObj *self)
{
    PyObject *pyresult;
    PyObject *pyproxy;
    PyObject *pyresult_list;
    PyObject *pystatus;
    
    if (self->done) {
        return 0;
    }
    
    self->done = 1;

    // Blocking call.
    pyresult_list = PyObject_CallMethod(self->queue, "get", NULL);

    if (!pyresult_list) {
        return -1;
    }

    pystatus = PySequence_GetItem(pyresult_list, 0); // NR.
    if (!pystatus) {
        Py_DECREF(pyresult_list);
        return -1;
    }
    
    if (PyLong_AsLong(pystatus) != 0) {
        // Error occurred.
        
        Py_DECREF(self->error);
        
        self->error = PySequence_GetItem(pyresult_list, 1); // NR.
    }
    else {
        // Success.
        
        Py_DECREF(self->result);
        
        pyresult = PySequence_GetItem(pyresult_list, 1); // NR.

        if (Py_TYPE(pyresult) == ContextTypePtr) {
            pyproxy = PyObject_CallFunction((PyObject *)
                                            DelegateProxyTypePtr,
                                            "Oi", pyresult, true);
            Py_DECREF(pyresult);
            pyresult = pyproxy;
        }

        self->result = pyresult;
    }
    Py_DECREF(pystatus);
    Py_DECREF(pyresult_list);
    
    return 0;
}

/** 
 * Steals ref.
 */
void
asyncresult_set_result(PyObject *asyncresult, PyObject *result)
{
    PyObject       *pyproxy;
    AsyncResultObj *self = (AsyncResultObj *)asyncresult;

    Py_DECREF(self->result);

    if (Py_TYPE(result) == ContextTypePtr) {
        pyproxy = PyObject_CallFunction((PyObject *)
                                        DelegateProxyTypePtr,
                                        "Oi", result, true);
        Py_DECREF(result);
        result = pyproxy;
    }

    self->result = result;
    self->done   = 1;
}

/** 
 * Steals ref.
 */
void
asyncresult_set_error(PyObject *asyncresult, PyObject *err)
{
    AsyncResultObj *self = (AsyncResultObj *)asyncresult;
    
    self->error     = err;
    self->done      = 1;
}




