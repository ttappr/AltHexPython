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

/*
static PyMemberDef Context_members[] = {
    {"_network",  T_OBJECT, offsetof(ContextObj,  _network), 0,
     "."},
    {"_channel", T_OBJECT, offsetof(ContextObj, _channel), 0,
     ""}, 
    {NULL}
};
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

static PyGetSetDef Context_accessors[] = {
    { "network",   (getter)Context_get_network,   (setter)NULL,
      "The network value for the context object.", NULL },
    { "channel",   (getter)Context_get_channel,    (setter)NULL,
      "The channel value for the context object.", NULL },
    { NULL }
};

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

PyTypeObject *ContextTypePtr = &ContextType;

/**
 * Constructor for Context objects. If no parameters are provided, the context
 * will be the currently active context.
 *
 * @param network   - specifies the server/network of the context. Optional - 
 *                    can be None.
 * @param channel   - specifies the channel of the context. Optional - can be
 *                    None.
 * @param context   - a capsule for a context pointer. Optional - other 
  *                   parameters are ignored if provided. 
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

static void
Context_dealloc(ContextObj *self)
{
    Py_XDECREF(self->ctx_capsule);
    Py_XDECREF(self->ctxptrval);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

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


PyObject *
Context_repr(ContextObj *self, PyObject *Py_UNUSED(ignored))
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

Py_hash_t
Context_hash(ContextObj *self)
{
    return PyObject_Hash(self->ctxptrval);
}

PyObject *
Context_cmp(ContextObj *a, PyObject *b, int op)
{
    ContextObj *ctb = (ContextObj *)b;

    if (Py_TYPE(b) == ContextTypePtr) {
        
        return PyObject_RichCompare(a->ctxptrval, ctb->ctxptrval, op);
    }
    Py_RETURN_FALSE;
}



    