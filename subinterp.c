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
 * This module provides access to per subinterpreter data stored within each 
 * subinterp's environment. It also has functions that execute within a
 * subinterp to configure it (setting up stdout/stderr, etc.). It also has
 * functions to switch threads.
 */

#include "minpython.h"

// Consts for quick access to per-interp data.
#define HC_DATA_KEY             "__hexchat_private__"
#define HC_HOOKS_KEY            0   
#define HC_UNLOAD_KEY           1   
#define HC_MAIN_THREADSTATE_KEY 2   
#define HC_QUEUE_MODULE         3
#define HC_THREADING_MODULE     4
#define HC_COLLECTIONS_MODULE   5
#define HC_LISTS_INFO           6
#define HC_NUM_KEYS             7 

/**
 * Callback information used for the custom unload event hook.
 */
typedef struct {
    PyObject *callable;
    PyObject *userdata;
} UnhookEventData;

 /**
 * Variables used by switch_threadstate() and switch_threadstate_back().
 */
static PyGILState_STATE py_gilstate;
static int              py_main_has_gil = 0;

/**
 * The main Python interpeter's thread state for the HexChat main thread.
 */
PyThreadState   *py_g_main_threadstate = NULL;

PyThreadState   *create_interp                  (interp_config_func, void *);
int             delete_interp                   (PyThreadState *, 
                                                 interp_config_func,
                                                 void *);
PyThreadState   *interp_get_main_threadstate    (void);
void            interp_add_hook                 (PyObject *);
PyObject        *interp_hook_unload             (PyObject *, PyObject *);
PyObject        *interp_unhook_unload           (PyObject *);
PyObject        *interp_get_hooks               (void);
PyObject        *interp_get_unload_hooks        (void);
PyObject        *interp_get_queue_constr        (void);
PyObject        *interp_get_namedtuple_constr   (void);
PyObject        *interp_get_lists_info          (void);
PyObject        *interp_get_plugin_name         (void);
int             interp_set_up_stdout_stderr     (void);

static void     interp_init_data                (PyThreadState *);
static void     interp_destroy_data             (void);

static 
inline PyObject *interp_get_data                (Py_ssize_t );

static void     py_hook_free_fn                 (PyObject *);

SwitchTSInfo    switch_threadstate              (PyThreadState *);
void            switch_threadstate_back         (SwitchTSInfo);

int             main_thread_check               (void);

/**
 * Creates a subinterpreter and sets up it's environment. 
 * @param configfunc - a callback to invoke after the subinterp has been 
 *                     configured. This can provide additional configuration.
 *                     It must return 0 on success, or non-zero on failure. If
 *                     non-zero, the created interp will be deleted before 
 *                     exiting.
 * @param data       - Like 'userdata' for other calls. The caller can provide
 *                     data it wants passed back to itself when invoking
 *                     configfunc().
 * @returns - pointer to new threadstate, or NULL ond failure.
 */
PyThreadState *
create_interp(interp_config_func configfunc, void *data)
{
    PyThreadState   *pynew_interp_threadstate;
    SwitchTSInfo    tsinfo;
    PyThreadState   *retval;
    
    retval = NULL;

    // Switch to the main Python interpreter's threadstate.
    tsinfo = switch_threadstate(py_g_main_threadstate);

    // Create a new sub-interp.
    pynew_interp_threadstate = Py_NewInterpreter();

    if (!pynew_interp_threadstate) {
        // There was a problem creating the interp.
        hexchat_print(ph,
            "\00304Unable to create new sub-interpreter.");
    }
    else {
        // Switch to the new interpreter.
        PyThreadState_Swap(pynew_interp_threadstate);

        // Initialize internal data.
        interp_init_data(pynew_interp_threadstate);

        // Set up the new sub-interp's standard output.
        interp_set_up_stdout_stderr();
        
        if (configfunc) {
            // Call customization callback.
            if (configfunc(pynew_interp_threadstate, data)) {
                delete_interp(pynew_interp_threadstate, NULL, NULL);
                retval = NULL;
            }
        }
        else {
            retval = pynew_interp_threadstate;
        }
    }
    // Switch back to the main interp threadstate.
    PyThreadState_Swap(py_g_main_threadstate);

    // Restore the initial threadstate.
    switch_threadstate_back(tsinfo);

    return retval;
}

/**
 * Deletes the interpreter.
 * @param ts         - The threadsate for the interpreter to delete.
 * @param configfunc - A callback to invoke as part of the teardown of the
 *                     interp.
 * @param data       - 'userdata' to pass back to configfunc() when invoked.
 * @returns - 0. Any erors that occur during teardown will be printed to the
 *            current hexchat window.
 */
int
delete_interp(PyThreadState *ts, interp_config_func configfunc, void *data)
{
    SwitchTSInfo tsinfo;
    PyObject    *pyunload_hooks;
    PyObject    *pycap;
    PyObject    *pycallable;
    PyObject    *pyuserdata;
    PyObject    *pyret;
    Py_ssize_t  size;
    UnhookEventData *evt_data;
    
    tsinfo = switch_threadstate(ts);

    if (PyThreadState_Get() == ts) {
        // Need to make sure the thread switch happened.
        // Failing to switch can happen on shutdown or unload.
        
        if (configfunc) {
            configfunc(ts, data);
        }
        
        pyunload_hooks = interp_get_unload_hooks();
        
        size = PyObject_Length(pyunload_hooks);
        
        // Invoke callbacks for the unload event.
        for (int i = 0; i < size; i++) {
            pycap      = PyList_GetItem(pyunload_hooks, i); // BR
            
            evt_data   = PyCapsule_GetPointer(pycap, "unload_hook");
            pycallable = evt_data->callable;
            pyuserdata = evt_data->userdata;
            
            pyret = PyObject_CallFunction(pycallable, "O", pyuserdata);
            if (!pyret) {
                PyErr_Print();
            }
            else {
                Py_DECREF(pyret);
            }
        }
        
        // Delete the interp's private data.
        interp_destroy_data();
        
        // Kill the interp.
        Py_EndInterpreter(ts);

        if (PyErr_Occurred()) {
            PyErr_Print();
        }
    }
    switch_threadstate_back(tsinfo);
    return 0;
}

/**
 * Sets up data objects for storing specific data for each subinterpreter.
 * @param ts    - The threadstate of the interpreter.
 */
void
interp_init_data(PyThreadState *ts)
{
    PyObject *pymain;
    PyObject *pydict;
    PyObject *pytup;
    PyObject *pyhooks;
    PyObject *pyunload;
    PyObject *pytscap;
    PyObject *pymodule;
    PyObject *pystr;

    pymain = PyImport_AddModule("__main__");    // BR
    pydict = PyModule_GetDict(pymain);          // BR
    pytup  = PyTuple_New(HC_NUM_KEYS);

    // Add __hexchat_private__ to the main environment for the subinterp.
    PyDict_SetItemString(pydict, HC_DATA_KEY, pytup);

    Py_DECREF(pytup);

    pyhooks  = PyList_New(0);
    pyunload = PyList_New(0);

    PyTuple_SetItem(pytup, HC_HOOKS_KEY, pyhooks);  // Steals ref
    PyTuple_SetItem(pytup, HC_UNLOAD_KEY, pyunload);

    pytscap = PyCapsule_New(ts, "threadstate", NULL);

    PyTuple_SetItem(pytup, HC_MAIN_THREADSTATE_KEY, pytscap);

    // Need to use PyImport_Import() to make sure the queue package is loaded
    // correctly. Using other functions worked, but there were missing 
    // dependencies for some reason. It would get weird after a plugin was
    // unloaded then loaded again.
    pystr         = PyUnicode_FromString("queue");
    pymodule      = PyImport_Import(pystr); // NR.
    PyTuple_SetItem(pytup, HC_QUEUE_MODULE, pymodule);
    Py_DECREF(pystr);
    
    pystr         = PyUnicode_FromString("threading");
    pymodule      = PyImport_Import(pystr);
    PyTuple_SetItem(pytup, HC_THREADING_MODULE, pymodule);
    Py_DECREF(pystr);

    pystr         = PyUnicode_FromString("collections");
    pymodule      = PyImport_Import(pystr);
    PyTuple_SetItem(pytup, HC_COLLECTIONS_MODULE, pymodule);
    Py_DECREF(pystr);
    
    PyTuple_SetItem(pytup, HC_LISTS_INFO, PyDict_New());
}

/**
 * Returns the queue.Queue constructor - used by AsyncResult's.
 */
PyObject *
interp_get_queue_constr()
{
    PyObject *pymod;
    PyObject *pydict;
    PyObject *pyconstr;

    pymod    = interp_get_data(HC_QUEUE_MODULE);
    pydict   = PyModule_GetDict(pymod);
    pyconstr = PyDict_GetItemString(pydict, "Queue");

    return pyconstr; // BR.
}

/**
 * Returns the constructor for collections.namedtuple, which is used by
 * hexchat.get_list() to construct list items.
 */
PyObject *
interp_get_namedtuple_constr()
{
    PyObject *pymod;
    PyObject *pydict;
    PyObject *pyconstr;

    pymod    = interp_get_data(HC_COLLECTIONS_MODULE);
    pydict   = PyModule_GetDict(pymod);
    pyconstr = PyDict_GetItemString(pydict, "namedtuple");

    return pyconstr; // BR.

}

/**
 * Returns the tuple object that holds an interpreter's private data.
 */
PyObject *
interp_get_lists_info(void)
{
    return interp_get_data(HC_LISTS_INFO);
}

/**
 * Ensures that the destructors are called on data objects. The hooks depend
 * on this when a plugin is being unloaded. The hook capsule free functions
 * need to get invoked to call hexchat_unhook().
 */
void
interp_destroy_data()
{
    PyObject *pymain;
    PyObject *pydict;
    
    pymain = PyImport_AddModule("__main__");    // BR
    pydict = PyModule_GetDict(pymain);          // BR
    
    // This forces the destructors for the capsules to get called.
    PyDict_DelItemString(pydict, HC_DATA_KEY);
}

/**
 * Returns a borrowed reference to the requested data object.
 * @param key   - One of the key's #define'd above.
 * @returns - The requested object.
 */
PyObject *
interp_get_data(Py_ssize_t key)
{
    PyObject *pymain;
    PyObject *pydict;
    PyObject *pytup;

    pymain = PyImport_AddModule("__main__");
    pydict = PyModule_GetDict(pymain);
    pytup  = PyDict_GetItemString(pydict, HC_DATA_KEY); // BR

    return PyTuple_GetItem(pytup, key); // BR
}

/**
 * Returns a string object for the name of the plugin (__module_name__).
 */
PyObject *
interp_get_plugin_name()
{
    PyObject *pymain;
    PyObject *pydict;
    PyObject *pyret;

    pymain = PyImport_AddModule("__main__");
    pydict = PyModule_GetDict(pymain);
    pyret  = PyDict_GetItemString(pydict, "__module_name__"); // BR.
    if (!pyret) {
        PyErr_Clear();
        pyret = PyUnicode_FromString("");
    }
    else {
        Py_INCREF(pyret);
    }
    return pyret;
}

/**
 * Returns the *main* threadstate for the current subinterpreter. The 
 * subinterpreter may have multiple threadstates, but each may need access to 
 * the main threadstate in order to register callbacks and that sort of thing.
 */
PyThreadState *
interp_get_main_threadstate()
{
    PyObject *pymtcap;
    
    pymtcap = interp_get_data(HC_MAIN_THREADSTATE_KEY);
    
    return (PyThreadState *)PyCapsule_GetPointer(pymtcap, "threadstate");
}

/**
 * Adds a hook capsule to the list of hooks for the current subinterpreter.
 * @param hook - the hook to add.
 */
void
interp_add_hook(PyObject *hook)
{
    PyObject *pyhooks;

    pyhooks = interp_get_data(HC_HOOKS_KEY);
    PyList_Append(pyhooks, hook);
}

/**
 * Adds a callback that will get invoked when the plugin is being unloaded.
 * These are a customization for Python - the C API doesn't have specific
 * hooks for the unloading event. Returns a capsule.
 * @param callback  - The unload event callback being registered.
 * @param userdata  - Data to pass back to the callback on the event.
 * @returns - A PyCapsule object holding a pointer to the hook data.
 */
PyObject *
interp_hook_unload(PyObject *callback, PyObject *userdata)
{
    PyObject        *pyhooks;
    PyObject        *pycap;
    UnhookEventData *evt_data;
    
    pyhooks = interp_get_data(HC_UNLOAD_KEY);
    
    evt_data = (UnhookEventData *)PyMem_RawMalloc(sizeof(UnhookEventData));
    evt_data->callable = callback;
    evt_data->userdata = userdata;

    Py_INCREF(callback);
    Py_INCREF(userdata);
    
    pycap = PyCapsule_New(evt_data, "unload_hook", py_hook_free_fn);
    
    PyList_Append(pyhooks, pycap);
    
    return pycap;
}

/**
 * Unhooks callbacks for the custom unload event. Returns the userdata that was
 * registered with the hook callback.
 * @param hook  - The hook for the callback to unhook.
 * @returns - The userdata that was registered with the callback.
 */
PyObject *
interp_unhook_unload(PyObject *hook) {
    PyObject        *pyhooks;
    PyObject        *pyret;
    UnhookEventData *evt_data;
    
    evt_data = (UnhookEventData *)PyCapsule_GetPointer(hook, "unload_hook");
    
    pyhooks = interp_get_unload_hooks();
    
    pyret = PyObject_CallMethod(pyhooks, "remove", "O", hook);
    if (pyret) {
        Py_DECREF(pyret);
    }
    else {
        PyErr_Clear();
    }
    Py_INCREF(evt_data->userdata);
    
    return evt_data->userdata;
}

/**
 * Returns a mutable list of hooks. 
 */
PyObject *
interp_get_hooks()
{
    return interp_get_data(HC_HOOKS_KEY);
}

/**
 * Returns a mutable list of unload event hooks.
 */
PyObject *
interp_get_unload_hooks()
{
    return interp_get_data(HC_UNLOAD_KEY);
}


/**
 * Sets the currently active Python (sub)interpreter's sys.stdout and sys.stderr
 * to hexchat.OutStream objects, thus enabling interp output and error messages.
 * @returns - 0 on success, -1 on failure.
 */
int interp_set_up_stdout_stderr()
{
    static const int IRC_RED = 4;
    PyObject *pyoutput;
    PyObject *pymodule;

    // Import the module to initialize the module's type objects.
    pymodule = PyImport_ImportModule("hexchat");

    if (!pymodule) {
        hexchat_print(ph, "Problem during plugin init loading hexchat "
                           "module.");
        return -1;
    }

    // Create OutStream for stderr.
    pyoutput = PyObject_CallFunction((PyObject *)OutStreamTypePtr,
                                     "Ni", PySys_GetObject("stderr"), IRC_RED);
    if (!pyoutput) {
        hexchat_printf(ph, "\0034Error encountered in hexchat.OutStream "
                           "constructor (sys.stderr = <<failed>>).");
        Py_DECREF(pymodule);
        return -1;
    }
    // [Python>>] sys.stderr = hexchat.OutStream(sys.stderr, IRC_RED)
    PySys_SetObject("stderr", pyoutput);
    Py_DECREF(pyoutput);

    // Create OutStream for stdout.
    pyoutput = PyObject_CallFunction((PyObject *)OutStreamTypePtr,
                                     "N", PySys_GetObject("stdout"));
    if (!pyoutput) {
        PyErr_Print();
        Py_DECREF(pymodule);
        return -1;
    }
    // [Python>>] sys.stdout = hexchat.OutStream(sys.stdout)
    PySys_SetObject("stdout", pyoutput);
    Py_DECREF(pyoutput);
    Py_DECREF(pymodule);
    return 0;
}

/**
 * Switches to the requested threadstate. There must be a matching call to
 * switch_threadstate_back() for each call to this function. If any other
 * threadstate related calls are made between these functions, they must
 * restore the state back to how it was just after this call. These functions
 * are meant to only be invoked in hexchat callbacks - which are always on the
 * hexchat main thread.
 *
 * @param   ts  - The threadstate to switch to.
 * @returns     - The prior threadstate data for use in the call to
 *                switch_threadstate_back()
 */
SwitchTSInfo
switch_threadstate(PyThreadState *ts)
{
    PyThreadState *pycur_ts;
    SwitchTSInfo  tsinfo = { 0, 0, NULL };

    if (!py_main_has_gil) {
        // Main thread grabs the GIL.
        py_gilstate = PyGILState_Ensure();
        pycur_ts = PyThreadState_Get();
        py_main_has_gil = 1;
        tsinfo.do_release = 1;
    }
    else {
        // There is already an active sub-interpreter threadstate in the 
        // hexchat main thread.
        pycur_ts = PyThreadState_Get();
    }

    if (ts != pycur_ts) {
        // A switch to a different threadstate has been requested. Swap to it.
        tsinfo.prior = PyThreadState_Swap(ts);
        tsinfo.do_swap = 1;
    }

    return tsinfo;
}

/**
 * Restores the requested threadstate. There must be a matching call to
 * switch_threadstate() for for each call to this function. If any other
 * threadstate related calls are made between these two functions, the state
 * must be put back to how it was just after the switch_threadstate() call
 * before calling switch_threadstate_back().
 *
 * @param   tsinfo - The previous threadstate data used to switch back.
 */
void
switch_threadstate_back(SwitchTSInfo tsinfo)
{
    if (tsinfo.do_swap) {
        // Swapping back to previous threadstate.
        PyThreadState_Swap(tsinfo.prior);
    }
    if (tsinfo.do_release) {
        // All done. Release the GIL.
        PyGILState_Release(py_gilstate);
        py_main_has_gil = 0;
    }
}


/**
 * Used to protect API calls from being called on any thread other than the
 * main thread. Returns 0 if execution is currently on the main thread, and
 * -1 if not. If not on the main thread, the current interpreter's error state
 * will be set.
 */
int
main_thread_check() 
{
    PyThreadState *pyts;
    pyts = PyThreadState_Get();
    
    if (pyts->thread_id != py_g_main_threadstate->thread_id) {

        PyErr_SetString(PyExc_RuntimeError,
                "The HexChat API should be called from the main thread when "
                "invoked directly. A Delegate may be created, which "
                "will execute API calls on the main thread when invoked from "
                "other threads. The `synchronous` and `asynchronous` objects "
                "of the module provide ready-made delegates for the API.");

        return -1;
    }
    return 0;
}

/**
 * A function used by the PyCapsule object that holds hook pointers to free
 * the unload_hook data.
 * @param pyhook - The hook for the callback data being freed.
 */
void
py_hook_free_fn(PyObject *pyhook)
{
    UnhookEventData *hook_data;
    
    hook_data = (UnhookEventData *)PyCapsule_GetPointer(pyhook, "unload_hook");
    
    Py_DECREF(hook_data->callable);
    Py_DECREF(hook_data->userdata);
    
    PyMem_RawFree(hook_data);
}
