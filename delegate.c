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
 * The Delegate is a callable wrapper for other callables. Delegates execute
 * their wrapped functions on the HexChat main thread and return the result
 * back to the caller (synchronous), or an AsyncResult object (asynchronous)
 * that can be used by the caller to retrieve the result or error - or it can
 * be ignored.
 */

#include "minpython.h"

/**
 * Delegate Python object instance data.
 */
typedef struct {
    PyObject_HEAD
    PyObject        *callable;
    PyObject        *queue_constr;
    int             is_async;
} DelegateObj;

/**
 * Data passed to timer callbacks used to invoke the internal callable on the
 * main thread.
 */
typedef struct {
    PyObject      *callable;
    PyObject      *args;
    PyObject      *kwargs;
    PyObject      *queue;
    int           is_async;
    PyThreadState *threadstate;
} DelegateData;

static int      Delegate_init           (DelegateObj *, PyObject *, PyObject *);
static void     Delegate_dealloc        (DelegateObj *);
static PyObject *Delegate_call          (DelegateObj *, PyObject *, PyObject *);
static PyObject *Delegate_get_is_async  (DelegateObj *, void *);

static PyObject *delegate_get_queue_constr  (DelegateObj *);
static int      delegate_timer_callback     (void *);

/**
 * Delegate accessor functions to be registered with the type.
 */
static PyGetSetDef Delegate_accessors[] = {
    {"is_async",  (getter)Delegate_get_is_async,  (setter)NULL,
     "If True, the delegate returns an AsyncResult immediately. "
     "If False, the delegate blocks until the wrapped callable "
     "completes and returns its result.", NULL},

    {NULL}
};

/**
 * Delegate type definition/instance.
 */
static PyTypeObject DelegateType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name        = "hexchat.Delegate",
    .tp_doc         = "Delegates are thread-safe function call "
                      "wrappers that execute on the HexChat main thread. "
                      "The Constructor takes the wrapped function/callable "
                      "as the first parameter, and the optional argument "
                      "`is_async` (False by default).",
    .tp_basicsize   = sizeof(DelegateObj),
    .tp_itemsize    = 0,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_new         = PyType_GenericNew,
    .tp_init        = (initproc)Delegate_init,
    .tp_dealloc     = (destructor)Delegate_dealloc,
    //.tp_members     = Delegate_members,
    .tp_call        = (ternaryfunc)Delegate_call,
    //.tp_methods     = Delegate_methods,
    .tp_getset      = Delegate_accessors,
};

/**
 * Convenient Delegate type pointer.
 */
PyTypeObject *DelegateTypePtr = &DelegateType;

/**
 * Delegate constructor.
 * @param self  - Delegate instance.
 * @param args  - 'callable', 'is_async' from Python. The first param is a
 *                Python callable object to be wrapped, and the second is a
 *                Python boolean value - True if the Delegate makes asynch
 *                calls to the main thread, or False for synchronous calls.
 * @returns - 0 on success, -1 on failure with error state set.
 */
int
Delegate_init(DelegateObj *self, PyObject *args, PyObject *kwargs)
{
    PyObject *pycallable    = NULL;
    int      is_async       = false;    

    static char *keywords[] = { "callable", "is_async", NULL };

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|p:init", keywords,
                                     &pycallable, &is_async)) {
        return -1;
    }
    if (PyCallable_Check(pycallable) == 0) {
        PyErr_SetString(PyExc_TypeError,
                        "Delegate constructor requires a callable object." );
        return -1;
    }
    self->callable = pycallable;
    self->is_async = is_async;

    Py_INCREF(pycallable);
    
    // Get the queueQueue constructor for creating thread-safe queues.
    self->queue_constr = delegate_get_queue_constr(self);
    if (!self->queue_constr) {
        return -1;
    }

    return 0;
}

/**
 * Destructor.
 */
void
Delegate_dealloc(DelegateObj *self)
{
    Py_XDECREF(self->callable);
    Py_XDECREF(self->queue_constr);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/**
 * Implements Delegate.__call__() which makes Delegate objects invokable as a
 * function. When invoked, the wrapped callable is put in the HexChat timer
 * queue for execution on the main thread. The result of this call will be
 * sent back to this function via a Queue object and returned, or an
 * AsyncResult will be immediately returned if the Delegate was created with
 * 'is_async' True.
 * @param self      - Delegate instance.
 * @param args      - Similar to *args in Python. The positional arguments
 *                    that will be forwarded to the wrapped callable.
 * @param kwargs    - Similar to **kwargs from Python. The keyword arguments
 *                    that are forwarded to the wrapped callable on invokation.
 * @returns - Either an AsyncResult object for async delegates, or the result
 *            of the wrapped call. NULL returned on failure with error state
 *            set.
 */
PyObject *
Delegate_call(DelegateObj *self, PyObject *args, PyObject *kwargs)
{
    PyObject      *pyret        = NULL;
    PyObject      *pyasync_result;
    PyObject      *pyqueue;
    PyObject      *pytemp;
    PyObject      *pyexc        = NULL;
    PyObject      *pyexc_type   = NULL;
    PyObject      *pytraceback  = NULL;
    PyObject      *pystatus;
    PyObject      *pyresult;
    long          status;
    DelegateData  *data;
    PyThreadState *pycur_threadstate;

    pycur_threadstate = PyThreadState_Get();

    if (pycur_threadstate->thread_id == py_g_main_threadstate->thread_id) {
        // This is being called on the HexChat main thread; just invoke the 
        // callable.
        pyret = PyObject_Call(self->callable, args, kwargs);
        
        if (self->is_async) {
            // User expects an AsyncResult back - even if calling an async
            // delegate on the main thread. So make one and return it.
            
            if (!pyret) {
                // Error occurred. Get the exception with __traceback__ set.
                PyErr_Fetch(&pyexc_type, &pyexc, &pytraceback);
                PyErr_NormalizeException(&pyexc_type, &pyexc, &pytraceback);
                if (pytraceback != NULL) {
                    PyException_SetTraceback(pyexc, pytraceback);
                }
                Py_DECREF(pyexc_type);
                Py_DECREF(pytraceback);
            }
            
            pyasync_result = PyObject_CallFunction((PyObject *)
                                                    AsyncResultTypePtr, "O",
                                                    Py_None);
            if (pyret) {
                asyncresult_set_result(pyasync_result, pyret); // Steals ref.
            }
            else {
                asyncresult_set_error(pyasync_result, pyexc); // Steals ref.
            }
            pyret = pyasync_result;
        }
    }
    else {
        // This is not the main thread; set up a timer callback to invoke the
        // callable.
        data = PyMem_RawMalloc(sizeof(DelegateData));

        data->callable    = self->callable;
        data->args        = args;
        data->kwargs      = kwargs;
        data->is_async    = self->is_async;
        data->threadstate = interp_get_main_threadstate(); 

        pyqueue = PyObject_CallFunction(self->queue_constr, "i", 1);
        if (!pyqueue) {
            return NULL;
        }
        data->queue = pyqueue;

        Py_INCREF(data->callable);
        Py_INCREF(data->queue);
        Py_XINCREF(args);
        Py_XINCREF(kwargs);

        // Put the invokation information on the timer queue.
        hexchat_hook_timer(ph, 0, delegate_timer_callback, (void *)data);

        if (!self->is_async) {
            // Synchronous call. Do a blocking read on the queue and wait for
            // the result.
            pyret = PyObject_CallMethod(pyqueue, "get", NULL);
            
            // Get the result list, check result, handle result or error. 
            // If error.. set the interp error state.
            
            pystatus = PySequence_GetItem(pyret, 0); // NR.
            pyresult = PySequence_GetItem(pyret, 1);

            status = PyLong_AsLong(pystatus);
            
            Py_DECREF(pystatus);
            Py_DECREF(pyret);
            Py_DECREF(pyqueue);
                
            if (status == 0) {
                pyret = pyresult;
            }
            else {
                // For synchronous calls that have an exception, restore the
                // exception so the caller gets it.
                pyret = NULL;
                PyErr_Restore(PyObject_Type(pyresult),  // Steals all refs.
                              pyresult, 
                              PyObject_GetAttrString(pyresult, 
                                                     "__traceback__"));
            }
        }
        else {
            // Async call. Create an AsyncResult object and return it.
            pyasync_result = PyObject_CallFunction((PyObject *)
                                                   AsyncResultTypePtr, "O", 
                                                   pyqueue);
            Py_DECREF(pyqueue);
            pyret = pyasync_result;
        }
    }
    if (pyret && Py_TYPE(pyret) == ContextTypePtr) {
        pytemp = PyObject_CallFunction((PyObject *)
                                       DelegateProxyTypePtr,
                                       "Oi", pyret, self->is_async);
        Py_DECREF(pyret);
        pyret = pytemp;
    }
    return pyret;
}

/**
 * The callback passed to hexchat_hook_timer() that invokes the Delegate's
 * wrapped callable on the main thread of HexChat.
 * @param userdata  - The userdata passed to hexchat_hook_timer().
 * @returns - 0 so the timer won't be rescheduled.
 */
int 
delegate_timer_callback(void *userdata)
{
    DelegateData    *data;
    PyObject        *pyret;
    PyObject        *pylist;
    PyObject        *pytemp;
    PyObject        *pyexc          = NULL;
    PyObject        *pyexc_type     = NULL;
    PyObject        *pytraceback    = NULL;
    SwitchTSInfo    tsinfo;
    
    data = (DelegateData *)userdata;

    // Switch to the userdata sub-interpreter's threadstate, grabbing the GIL.
    tsinfo = switch_threadstate(data->threadstate);

    // Invoke the function/method.
    pyret = PyObject_Call(data->callable, data->args, data->kwargs);

    if (!pyret) {
        // Error occurred. Get the exception with __traceback__ set.
        PyErr_Fetch(&pyexc_type, &pyexc, &pytraceback);
        PyErr_NormalizeException(&pyexc_type, &pyexc, &pytraceback);
        if (pytraceback != NULL) {
            PyException_SetTraceback(pyexc, pytraceback);
        }
        Py_DECREF(pyexc_type);
        Py_DECREF(pytraceback);
    }
    
    // Create result list and populate with either error or result.
    pylist = PyList_New(2);
    
    if (pyret) {
        // Success.
        PyList_SetItem(pylist, 0, PyLong_FromLong(0)); // Steals ref.
        PyList_SetItem(pylist, 1, pyret);
    }
    else {
        // Error.
        PyList_SetItem(pylist, 0, PyLong_FromLong(-1));
        PyList_SetItem(pylist, 1, pyexc);
    }
    
    // Send the result data back to the caller.
    pytemp = PyObject_CallMethod(data->queue, "put", "O", pylist);
    
    Py_DECREF(pylist);
    Py_DECREF(pytemp);
    Py_DECREF(data->callable);
    Py_DECREF(data->queue);
    Py_XDECREF(data->args);
    Py_XDECREF(data->kwargs);

    PyMem_RawFree(data);

    // Switch back to the prior threadstate and release the GIL.
    switch_threadstate_back(tsinfo);

    // Return 0 so this callback isn't put back on the timer queue.
    return 0;
}

/**
 * Used to retrieve queue.Queue so queue objects can be created to pass
 * results from the timer callback to a Delegate object invoked from another
 * thread to the caller.
 */
PyObject *
delegate_get_queue_constr(DelegateObj *self)
{
    PyObject *pyqconstr;

    pyqconstr = interp_get_queue_constr();
    Py_INCREF(pyqconstr);
    return pyqconstr;
}

/**
 * Delegate.is_async accessor function. Returns True if 'is_async' is set for
 * the delegate, or False if not.
 */
PyObject *
Delegate_get_is_async(DelegateObj *self, void *closure)
{
    if (self->is_async) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}












