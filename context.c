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
 * Context objects represent the context pointer returned by
 * hexchat_find_context(), or hexchat_get_context(). The objects implement
 * __repr__() for intuitve display, __cmp__() for comparisons, __hash__() to
 * support use of Context objects as dict keys.  The context also provides
 * a small subset of the HexChat API that will execute within the associated
 * context when invoked.
 */

#include "minpython.h"

/**
 * Context instance data.
 */
typedef struct {
    PyObject_HEAD
    PyObject *ctx_capsule;
    PyObject *ctxptrval;
    void     *ctxptr;
    int      fields_set;
} ContextObj;

static int      Context_init            (ContextObj *, PyObject *, PyObject *);
static void     Context_dealloc         (ContextObj *);
static PyObject *Context_set            (ContextObj *, PyObject *);
static PyObject *Context_prnt           (ContextObj *, PyObject *);
static PyObject *Context_emit_print     (ContextObj *, PyObject *, PyObject *);
static PyObject *Context_command        (ContextObj *, PyObject *);
static PyObject *Context_get_info       (ContextObj *, PyObject *);
static PyObject *Context_get_list       (ContextObj *, PyObject *);
static PyObject *Context_get_listiter   (ContextObj *, PyObject *);
static PyObject *Context_repr           (ContextObj *, PyObject *);

static Py_hash_t Context_hash           (ContextObj *);
static PyObject *Context_cmp            (ContextObj *, PyObject *, int);

static PyObject *Context_get_network    (ContextObj *, void *);
static PyObject *Context_get_channel    (ContextObj *, void *);

static inline int set_ctx               (ContextObj *, hexchat_context **);

/**
 * Methods provided by Context objects.
 */
static PyMethodDef Context_methods[] = {
    {"set",        (PyCFunction)Context_set,      METH_NOARGS, 
     "Changes the current context to this one."},
     
    {"prnt",       (PyCFunction)Context_prnt,     METH_VARARGS,
     "Prints message to the window associated with this Context."},
      
    {"emit_print", (PyCFunction)Context_emit_print, 
                                                  METH_VARARGS | METH_KEYWORDS,
     "Generates a print event with the given arguments in this Context."}, 
     
    {"command",    (PyCFunction)Context_command,  METH_VARARGS,
     "Executes a command as if typed into HexChat's input box from this "
     "Context."},
     
    {"get_info",   (PyCFunction)Context_get_info, METH_VARARGS,
     "Returns information based on this Context."},
     
    {"get_listiter",
                   (PyCFunction)Context_get_listiter,
                                                  METH_VARARGS,
    "Retrieves an iterator for lists of information from this Context."},

    {"get_list",   (PyCFunction)Context_get_list, METH_VARARGS,
     "Retrieves lists of information from this Context."},

    {NULL}
};

/**
 * Properties for Context objects.
 */
static PyGetSetDef Context_accessors[] = {
    { "network",   (getter)Context_get_network,   (setter)NULL,
      "The network value for the context object.", NULL },
    { "channel",   (getter)Context_get_channel,    (setter)NULL,
      "The channel value for the context object.", NULL },
    { NULL }
};

/**
 * Context type declaration/instance.
 */
static PyTypeObject ContextType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name        = "hexchat.Context",
    .tp_doc         = "Represents a context.",
    .tp_basicsize   = sizeof(ContextObj),
    .tp_itemsize    = 0,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_new         = PyType_GenericNew,
    .tp_init        = (initproc)Context_init,
    .tp_dealloc     = (destructor)Context_dealloc,
    //.tp_members     = Context_members,
    .tp_methods     = Context_methods,
    .tp_getset      = Context_accessors,
    .tp_repr        = (reprfunc)Context_repr,
    .tp_hash        = (hashfunc)Context_hash,
    .tp_richcompare = (richcmpfunc)Context_cmp,
};

/**
 * Convenience pointer to Context type.
 */
PyTypeObject *ContextTypePtr = &ContextType;

/**
 * Constructor for Context objects. If no parameters are provided, the context
 * will be the currently active context.
 * Python accessible params:
 * @param network   - specifies the server/network of the context. Optional - 
 *                    can be None.
 * @param channel   - specifies the channel of the context. Optional - can be
 *                    None.
 * @param context   - a capsule for a context pointer. Optional - other 
 *                    parameters are ignored if provided.
 * @returns 0 on success, -1 on failure.
 */
static int
Context_init(ContextObj *self, PyObject *args, PyObject *kwargs)
{
    PyObject        *pynetwork  = Py_None;
    PyObject        *pychannel  = Py_None;
    PyObject        *pyctx_cap  = Py_None;
    const char      *network    = NULL;
    const char      *channel    = NULL;
    hexchat_context *ctx;
    int             retval      = 0;
    static char     *keywords[] = { "network", "channel", "context", NULL };
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OOO:Context.__init__", 
                                     keywords, &pynetwork, &pychannel, 
                                     &pyctx_cap)) {
        return -1;
    }
    if (pyctx_cap != Py_None && 
        (!PyCapsule_CheckExact(pyctx_cap) || 
          strcmp(PyCapsule_GetName(pyctx_cap), "context"))) {

        PyErr_SetString(PyExc_TypeError,
                        "Context.__init__(), the context parameter must be a "
                        "context capsule.");
        return -1;
    }
    if (((pynetwork != Py_None) && (!PyUnicode_Check(pynetwork))) ||
        ((pychannel != Py_None) && (!PyUnicode_Check(pychannel)))) {
        PyErr_SetString(PyExc_TypeError,
                        "Context.__init__(), network and channel parameters "
                        "must be either unicode or None.");
        return -1;

    }
    if (pyctx_cap != Py_None) {
        self->ctx_capsule   = pyctx_cap;
        self->ctxptr        = PyCapsule_GetPointer(pyctx_cap, "context");
        self->ctxptrval     = PyLong_FromVoidPtr((void *)self->ctxptr);

        Py_INCREF(pyctx_cap);

    }
    else {
        if (pynetwork != Py_None) {
            network = PyUnicode_AsUTF8(pynetwork);
        }
        if (pychannel != Py_None) {
            channel = PyUnicode_AsUTF8(pychannel);
        }
        ctx = hexchat_find_context(ph, network, channel);

        if (ctx) {
            self->ctx_capsule   = PyCapsule_New(ctx, "context", NULL);
            self->ctxptr        = ctx;
            self->ctxptrval     = PyLong_FromVoidPtr((void *)ctx);
        }
        else {
            retval = -1;
        }
    }
    if (retval) {
        PyErr_SetString(PyExc_RuntimeError,
                        "Invalid context parameters.");
    }

    return retval;
}

/**
 * Destructor.
 */
static void
Context_dealloc(ContextObj *self)
{
    Py_XDECREF(self->ctx_capsule);
    Py_XDECREF(self->ctxptrval);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/**
 * Sets HexChat to the context pointed to by self->ctxptr.
 * @param self      - A Context instance.
 * @param prior_ctx - A pointer to store the prior context in before switching
 *                    to self->ctxptr.
 * @returns - 0 on success, -1 on failure with error state set.
 */
int
set_ctx(ContextObj *self, hexchat_context **prior_ctx)
{
    int      result;
    int      retval = 0;
    
    *prior_ctx = hexchat_get_context(ph);

    result = hexchat_set_context(ph, self->ctxptr);

    if (!result) {
        PyErr_SetString(PyExc_RuntimeError,
                        "Failed to switch to context.");
        retval = -1;
    }
    return retval;
}

/**
 * Implements Context.set(). Sets the HexChat active context to self->ctxptr.
 * @returns - None on success, NULL on failure with error state set.
 */
PyObject *
Context_set(ContextObj *self, PyObject *Py_UNUSED(ignored))
{
    hexchat_context *prior_ctx;
    
    if (main_thread_check()) {
        return NULL;
    }
    
    if (set_ctx(self, &prior_ctx)) {
        return NULL;
    }
    
    Py_RETURN_NONE;
}

/**
 * Implements Context.prnt().
 * @param self  - Context instance.
 * @param args  - 'text'. The text to print.
 * @returns - None on success, NULL on failure with error state set.
 */
PyObject *
Context_prnt(ContextObj *self, PyObject *args)
{
    PyObject        *pytext;
    hexchat_context *prior_ctx;
    
    if (main_thread_check()) {
        return NULL;
    }
    
    if (!PyArg_ParseTuple(args, "|U:Context.prnt", &pytext)) {
        return NULL;
    }
    
    if (set_ctx(self, &prior_ctx)) {
        return NULL;
    }
    hexchat_print(ph, PyUnicode_AsUTF8(pytext));
    
    hexchat_set_context(ph, prior_ctx);
    
    Py_RETURN_NONE;
}

/**
 * Implements Context.emit_print(). Forwards call to py_emit_print() declared
 * in minpython.c.
 * @param self   - Context instance.
 * @param args   - Parameters passed in from Python and forwarded to
 *                 py_emit_print().
 * @param kwargs - Keyword params forwarded to py_emit_print().
 * @returns - Python long object 0 value on failure, 1 on success.
 */
PyObject *
Context_emit_print(ContextObj *self, PyObject *args, PyObject *kwargs)
{
    hexchat_context *prior_ctx;
    PyObject        *pyret;

    if (main_thread_check()) {
        return NULL;
    }
    
    if (set_ctx(self, &prior_ctx)) {
        return NULL;
    }
    
    pyret = py_emit_print((PyObject *)self, args, kwargs);
    
    hexchat_set_context(ph, prior_ctx);
    
    return pyret;
}

/**
 * Implements Context.command().
 * @param self  - Context instance.
 * @param args  - 'text'. The text of the command passed in from Python.
 * @returns - None on success, NULL on fail with error state set.
 */
PyObject *
Context_command(ContextObj *self, PyObject *args)
{
    PyObject        *pytext;
    hexchat_context *prior_ctx;

    if (main_thread_check()) {
        return NULL;
    }
    
    if (!PyArg_ParseTuple(args, "U:Context.command", &pytext)) {
        return NULL;
    }
    if (set_ctx(self, &prior_ctx)) {
        return NULL;
    }

    hexchat_command(ph, PyUnicode_AsUTF8(pytext));

    hexchat_set_context(ph, prior_ctx);

    Py_RETURN_NONE;
}

/**
 * Implements Context.get_info(). Forwards call to py_get_info() declared in
 * minpython.c. Executes the function within the self->ctxptr context.
 * @param self  - Context instance.
 * @param args  - 'id' passed in from Python. The ID, or name, of the
 *                information requested.
 * @returns - A Python string or long object on success, NULL on failure with
 *            error state set.
 */
PyObject *
Context_get_info(ContextObj *self, PyObject *args)
{
    hexchat_context *prior_ctx;
    PyObject        *pyret;
    PyObject        *pyid;
    
    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "U:Context.get_info", &pyid)) {
        return NULL;
    }

    if (set_ctx(self, &prior_ctx)) {
        return NULL;
    }

    pyret = py_get_info((PyObject *)self, args);

    hexchat_set_context(ph, prior_ctx);

    return pyret;
}

/**
 * Implements Context.get_list(). Sets the context and forwards call to
 * py_get_list().
 * @param self  - Context instance.
 * @param args  - 'text'. The name of the list to retrieve.
 * @returns - A Python list object, or NULL on failure with error state set.
 */
PyObject *
Context_get_list(ContextObj *self, PyObject *args)
{
    PyObject        *pytext;
    hexchat_context *prior_ctx;
    PyObject        *pyret;

    if (main_thread_check()) {
        return NULL;
    }
    
    if (!PyArg_ParseTuple(args, "U:Context.get_list", &pytext)) {
        return NULL;
    }    
    if (set_ctx(self, &prior_ctx)) {
        return NULL;
    }

    pyret = py_get_list((PyObject *)self, args);
    
    hexchat_set_context(ph, prior_ctx);

    return pyret;
}

/**
 * Implements Context.get_listiter(). Sets context and forwards call to
 * py_get_listiter().
 * @param self  - Context instance.
 * @param args  - 'text'. The name of the list to get an iterator for.
 * @returns - A Python iterator object that can be used to iterate over the
 *            internal hexchat list and access its fields. NULL on failure.
 */
PyObject *
Context_get_listiter(ContextObj *self, PyObject *args)
{
    PyObject        *pytext;
    hexchat_context *prior_ctx;
    PyObject        *pyret;

    if (main_thread_check()) {
        return NULL;
    }
    
    if (!PyArg_ParseTuple(args, "U:Context.get_listiter", &pytext)) {
        return NULL;
    }    
    if (set_ctx(self, &prior_ctx)) {
        return NULL;
    }

    pyret = py_get_listiter((PyObject *)self, args);
    
    hexchat_set_context(ph, prior_ctx);

    return pyret;
}

/**
 * Implements the Context.__repr__() method to intuitive represent the object
 * to the user.
 * @param self  - instance.
 * @param args  - ignored.
 * @returns - A Python string object representing the Context instance. NULL
 *            on failure with error state set.
 */
PyObject *
Context_repr(ContextObj *self, PyObject *Py_UNUSED(args))
{
    PyObject *pyrepr;
    const char *network, *channel;
    hexchat_context *prior_ctx;
    
    if (set_ctx(self, &prior_ctx)) {
        return NULL;
    }
    
    network = hexchat_get_info(ph, "network");
    channel = hexchat_get_info(ph, "channel");
    
    hexchat_set_context(ph, prior_ctx);

    if (!channel) {
        pyrepr = PyUnicode_FromFormat("Context(network='%s', channel='')",
                                      network);
    }
    else {
        pyrepr = PyUnicode_FromFormat("Context(network='%s', channel='%s')",
                                      network, channel);
    }

    return pyrepr;
}

/**
 * Implements the Context.network property getter.
 * @param self      - instance.
 * @param closure   - not used.
 * @returns - A Python string object with the name of the context's network,
 *            NULL on failure with error state set.
 */
PyObject *
Context_get_network(ContextObj *self, void *closure)
{
    hexchat_context *prior_ctx;
    const char *network;
    
    if (set_ctx(self, &prior_ctx)) {
        return NULL;
    }
    network = hexchat_get_info(ph, "network");
    hexchat_set_context(ph, prior_ctx);
    
    return PyUnicode_FromString(network);
}

/**
 * Implements Context.channel property getter.
 * @param self      - Instance.
 * @param closure   - Not used.
 * @returns - A Python string object for the context's channel. NULL on fail.
 */
PyObject *
Context_get_channel(ContextObj *self, void *closure)
{
    hexchat_context *prior_ctx;
    const char *channel;
    
    if (set_ctx(self, &prior_ctx)) {
        return NULL;
    }
    channel = hexchat_get_info(ph, "channel");
    hexchat_set_context(ph, prior_ctx);
    
    return PyUnicode_FromString(channel);
}

/**
 * Implements Context.__hash__(). Returns a hash for the object.
 */
Py_hash_t
Context_hash(ContextObj *self)
{
    return PyObject_Hash(self->ctxptrval);
}

/**
 * Implements the rich comparison function for Context objects.
 */
PyObject *
Context_cmp(ContextObj *a, PyObject *b, int op)
{
    ContextObj *ctb = (ContextObj *)b;

    if (Py_TYPE(b) == ContextTypePtr) {
        
        return PyObject_RichCompare(a->ctxptrval, ctb->ctxptrval, op);
    }
    Py_RETURN_FALSE;
}



    
