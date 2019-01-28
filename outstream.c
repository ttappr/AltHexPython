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
 * OutStream objects handle standard output and are assigned to the sys.stdout
 * and sys.stderr of each subinterpreter and the Console interp. OutStream
 * objects provide a write() and flush() method and buffer output similar to
 * the default standard output objects of other Python shell environments. A
 * colorize() method is also provided to add IRC color codes to Python script
 * text for the Console, or any other code that may want to utilize the
 * feature. The Console's sys.stdout colorizes automatically by default on
 * stdout. This can be turned off via sys.stdout.colorize_on = False.
 *
 * The colorizer colors can be configured using set_pluginpref().  Set
 * 'string_color', 'number_color', 'operator_color', 'builtins_color',
 * 'keyword_color' to any of the color codes that start with 'hexchat.IRC_...'.
 * e.g. hexchat.set_pluginpref('string_color', hexchat.IRC_RED). The settings
 * will become effective after restarting HexChat, or you can do this:
 * sys.stdout = hexchat.OutStream(); sys.stdout.colorize_on = True.
 */

#include "minpython.h"

/**
 * OutStream object data. 
 */
typedef struct {
    PyObject_HEAD
    PyObject *orig_stream;
    PyObject *str_list;
    PyObject *emptystr;
    PyObject *newline;
    int      color;

    // For Python script colorization in console.
    int colorize_on;
    ColorizerParams colorizer_params;
    
} OutStreamObj;

/** 
 * Data used by timer_write() to print to the active context.
 */
typedef struct {
    char       *text;
    int        color;
    Py_ssize_t size;
} OutStreamData;

/**
 * Forward declarations. See actual declarations below for info on functions.
 */

static int      OutStream_init        (OutStreamObj *, PyObject *, PyObject *);
static void     OutStream_dealloc     (OutStreamObj *);
static PyObject *OutStream_write      (OutStreamObj *, PyObject *);
static PyObject *OutStream_flush      (OutStreamObj *, PyObject *);
static PyObject *OutStream_colorize   (OutStreamObj *, PyObject *);
static PyObject *OutStream_get_colorize_on
                                      (OutStreamObj *, void *);
static int      OutStream_set_colorize_on
                                      (OutStreamObj *, PyObject *, void *);

static int      colorize_init         (OutStreamObj *);
static PyObject *add_mono_color       (OutStreamObj *, PyObject *);
static int      timer_write           (void *);
inline void     print_string          (const char *, Py_ssize_t);

/**
 * OutStream member data.
 */
static PyMemberDef OutStream_members[] = {
    {"orig_stream",       T_OBJECT_EX, offsetof(OutStreamObj, orig_stream), 0, 
     "The original sys.stdout object before being replaced with an instance "
     "of this type."},

    {"color",             T_INT,       offsetof(OutStreamObj, color),       0,
     "The IRC color code as an integer to use in printing the output. "
     "E.g. IRC_RED = 4. If -1, output is not colorized."},

    {NULL}
};

/**
 * OutStream methods.
 */
static PyMethodDef OutStream_methods[] = {
    {"write",    (PyCFunction)OutStream_write,       METH_VARARGS,
     "Buffers (or writes out if \n terminated) text to the active HexChat "
     "context's window."},

    {"flush",    (PyCFunction)OutStream_flush,       METH_NOARGS,
     "Writes out all buffered text."},

    {"colorize", (PyCFunction)OutStream_colorize,    METH_VARARGS,
     "Adds IRC color codes to the provided string of Python code."},

    {NULL}
};

/**
 * OutStream get/set accessors.
 */
static PyGetSetDef OutStream_accessors[] = {
    {"colorize_on", (getter)OutStream_get_colorize_on,     
                    (setter)OutStream_set_colorize_on,
     "Python script code will be colorized when this is True.", NULL},
    
    {NULL}
};

/**
 * OutStream type definition.
 */
static PyTypeObject OutStreamType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name        = "hexchat.OutStream",
    .tp_doc         = "sys.stdout is set to this object to enable print() "
                      "output. sys.stderr is set to another instance to "
                      "enable error output from the interpreter in red.",
    .tp_basicsize   = sizeof(OutStreamObj),
    .tp_itemsize    = 0,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_new         = PyType_GenericNew,
    .tp_init        = (initproc)OutStream_init,
    .tp_dealloc     = (destructor)OutStream_dealloc,
    .tp_members     = OutStream_members,
    .tp_methods     = OutStream_methods,
    .tp_getset      = OutStream_accessors,
};

/**
 * Global type pointer for OutStream.
 */
PyTypeObject *OutStreamTypePtr = &OutStreamType;

/**
 * Constructor - initializes the OutStream object.
 */
static int
OutStream_init(OutStreamObj *self, PyObject *args, PyObject *kwargs)
{
    PyObject    *pyorig_stream  = Py_None;
    int         color           = -1;
    static char *keywords[]     = { "orig_stream", "color", NULL };
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|Oi:init", keywords, 
                                     &pyorig_stream, &color)) {
        return -1;
    }
    
    self->emptystr    = PyUnicode_FromString("");
    self->newline     = PyUnicode_FromString("\n");
    self->color       = color;
    self->orig_stream = pyorig_stream;
    
    Py_INCREF(pyorig_stream);
    
    self->str_list = PyList_New(0);
    return 0;
}

/**
 * Destructor - deallocates the OutStream object.
 */
static void
OutStream_dealloc(OutStreamObj *self)
{
    Py_XDECREF(self->orig_stream);
    Py_XDECREF(self->str_list);
    Py_XDECREF(self->emptystr);
    Py_XDECREF(self->newline);
    
    Py_XDECREF(self->colorizer_params.builtins_list);
    Py_XDECREF(self->colorizer_params.string_color);
    Py_XDECREF(self->colorizer_params.number_color);
    Py_XDECREF(self->colorizer_params.keyword_color);
    Py_XDECREF(self->colorizer_params.operator_color);
    Py_XDECREF(self->colorizer_params.origattr_color);
    Py_XDECREF(self->colorizer_params.comment_color);
    Py_XDECREF(self->colorizer_params.builtins_color);
    
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/**
 * "Buffers" the provided text for printing. If it's terminated with a newline, 
 * the buffer is flushed immediately and all pending text is written to the 
 * active context window.
 * @param self      - instance.
 * @param args      - 'text' from Python to write.
 * @returns - None on success, NULL on fail with error state set.
 */
PyObject *
OutStream_write(OutStreamObj *self, PyObject *args)
{
    PyObject    *pycmp;
    PyObject    *pytext;

    if (!PyArg_ParseTuple(args, "U:write", &pytext)) {
        return NULL;
    }
    // Append the text string to the list used to "buffer" output.
    PyList_Append(self->str_list, pytext);

    // Check for '\n' at the end.
    pycmp = PyObject_CallMethod(pytext, "endswith", "s", "\n");
    
    if (pycmp == Py_True) {
        // If the last character of the output string was '\n', call flush().
        OutStream_flush(self, NULL);
    }
    Py_DECREF(pycmp);
    
    Py_RETURN_NONE;
}

/**
 * "Flushes" the internal "buffer" and writes out the text to the active 
 * context.
 */
PyObject *
OutStream_flush(OutStreamObj *self, PyObject *Py_UNUSED(args))
{
    PyObject        *pystr;
    PyObject        *pycolstr;
    PyObject        *pylist;
    const char      *text;
    Py_ssize_t      size;
    OutStreamData   *data;
    PyThreadState   *pythreadstate;
    
    // Create a new "buffer" for the OutStream.
    pylist         = self->str_list;
    self->str_list = PyList_New(0);
    
    // Join the old "buffer" list into one string.
    pystr = PyUnicode_Join(self->emptystr, pylist);

    Py_DECREF(pylist);

    if (self->colorize_on) {
        // Colorize the string if Python code colorization is enabled.
        pycolstr = PyObject_CallMethod((PyObject *)
                                       self, "colorize", "O", pystr);
        Py_DECREF(pystr);
        pystr = pycolstr;
    }
    else if (self->color != -1) {
        // Mono color enabled. This is used for stderr output in red.
        pycolstr = add_mono_color(self, pystr);
        Py_DECREF(pystr);
        pystr = pycolstr;
    }
    
    // Get the current threadstate.
    pythreadstate = PyThreadState_Get(); // PyGILState_GetThisThreadState();
    
    // Get the text to be printed.
    text  = PyUnicode_AsUTF8AndSize(pystr, &size);
    
    if (pythreadstate->thread_id == py_g_main_threadstate->thread_id) {
        // This is the main thread. Just print the text.
        print_string(text, size);
    }
    else {
        // This isn't the main thread. Use the timer queue to print the 
        // text.
        size++;
        
        data = (OutStreamData *)PyMem_RawMalloc(sizeof(OutStreamData));
        data->color = self->color;
        data->text  = (char *)PyMem_RawMalloc(size);
        data->size = size;

        memcpy(data->text, text, size);

        hexchat_hook_timer(ph, 0, timer_write, data);
    }
    Py_DECREF(pystr);
    
    Py_RETURN_NONE;
}


/**
 * Colorizes the Python code string passed to it. This method is available
 * via OutStream objects.
 * @param self      - instance.
 * @param args      - 'str' from Python. The string of Python code to colorize.
 * @returns - Either the colorized string object, or NULL with error state set.
 */
PyObject *
OutStream_colorize(OutStreamObj *self, PyObject *args)
{
    PyObject *pystr;
    PyObject *pycolstr;

    if (!PyArg_ParseTuple(args, "U:colorize", &pystr)) {
        return NULL;
    }
    if (self->colorizer_params.builtins_color == NULL) {
        colorize_init(self);
    }
    Py_INCREF(pystr);

    pycolstr = flex_colorize(pystr, &self->colorizer_params);

    if (!pycolstr) {
        // If there's an error, just clear it and use the original string.
        PyErr_Clear();
    }
    else {
        if (PyUnicode_GetLength(pycolstr) >= PyUnicode_GetLength(pystr)) {
            // All OK - use the colorized string.
            Py_DECREF(pystr);
            pystr = pycolstr;
        }
        else {
            Py_XDECREF(pycolstr);
        }
    }
    return pystr;
}

/**
 * Returns True if code colorization is turned on; False otherwise.
 * @param self      - Instance.
 * @param closure   - Not used.
 * @returns - True if colorization is on, False otherwise.
 */
PyObject *
OutStream_get_colorize_on(OutStreamObj *self, void *closure)
{
    if (self->colorize_on) {
        Py_RETURN_TRUE;
    }
    else {
        Py_RETURN_FALSE;
    }
}

/**
 * Turns code colorization on or off for the OutStream object.
 * @param self      - Instance.
 * @param value     - From Python either True or False to set the property.
 * @returns - 0 on success, -1 on fail with error state set.
 */
int
OutStream_set_colorize_on(OutStreamObj *self, PyObject *value, void *closure)
{
    if (value == Py_True) {
        self->colorize_on = 1;
    }
    else if (value == Py_False) {
        self->colorize_on = 0;
    }
    else {
        PyErr_SetString(PyExc_TypeError,
            "colorize_on requres boolean type.");
        return -1;
    }
    if (value == Py_True && !self->colorizer_params.builtins_list) {
        if (colorize_init(self)) {
            self->colorize_on = 0;
            return -1;
        }
    }
    return 0;
}

/**
 * Timer callback that writes text to the current context.
 */
int
timer_write(void *userdata)
{
    OutStreamData *data = (OutStreamData *)userdata;

    print_string(data->text, data->size);
    
    PyMem_RawFree(data->text);
    PyMem_RawFree(data);
    return 0;
}

/**
 * Returns a string mono-colorized to the color code self->color. This is used
 * to display stderr output in red.
 * @param self  - instance.
 * @param pystr - The string to mono-colorize.
 */
PyObject *
add_mono_color(OutStreamObj *self, PyObject *pystr)
{
    PyObject *pyjoinstr;
    PyObject *pylist;
    PyObject *pycolstr;

    // Split the string on newlines and join it with the color code.

    pyjoinstr = PyUnicode_FromFormat("\003%i", self->color);
    pylist = PyUnicode_Splitlines(pystr, 1);

    PyList_Insert(pylist, 0, self->emptystr);

    pycolstr = PyUnicode_Join(pyjoinstr, pylist);

    Py_DECREF(pylist);
    Py_DECREF(pyjoinstr);

    return pycolstr;
}

/**
 * Used internally by colorize_init() to set the color for syntax items using
 * either pluginprefs, or the default colors.
 * @param pymodule      - The hexchat module.
 * @param syntax_item   - The name of the syntax item: 'string_color',
 *                        'number_color', 'keyword_color', 'operator_color',
 *                        'comment_color', 'builtins_color'.
 * @param default_color - The name of the default color to use: one of the
 *                        hexchat module attributes that begin with 'IRC_'.
 * @returns - The color code from either pluginprefs or the default.
 */
inline PyObject *
get_color(PyObject *pymodule, char *syntax_item, char *default_color)
{
    PyObject *pypref;

    pypref = PyObject_CallMethod(pymodule, "get_pluginpref", "s", syntax_item);

    if (pypref == Py_None) {
        Py_DECREF(pypref);
        pypref = PyObject_GetAttrString(pymodule, default_color);
    }

    return pypref;
}

/**
 * Initializes the colorization parameters - assigns colors to syntax items.
 * @returns - 0.
 */
int
colorize_init(OutStreamObj *self)
{
    PyObject *pybuiltins_module;
    PyObject *pyhcmodule;

    pyhcmodule        = PyImport_ImportModule("hexchat");
    pybuiltins_module = PyImport_ImportModule("builtins");

    ColorizerParams cp = {
        .builtins_list  = PyObject_Dir(pybuiltins_module),
        .origattr_color = PyObject_GetAttrString(pyhcmodule,
                                                 "IRC_ORIG_ATTRIBS"),

        .string_color   = get_color(pyhcmodule, "string_color",  "IRC_MAGENTA"),
        .number_color   = get_color(pyhcmodule, "number_color",  "IRC_CYAN"),
        .keyword_color  = get_color(pyhcmodule, "keyword_color", "IRC_NAVY"),
        .operator_color = get_color(pyhcmodule, "operator_color", "IRC_OLIVE"),
        .comment_color  = get_color(pyhcmodule, "comment_color",  "IRC_GREEN"),
        .builtins_color = get_color(pyhcmodule, "builtins_color", "IRC_TEAL"),
    };

    self->colorizer_params = cp;

    Py_DECREF(pybuiltins_module);
    Py_DECREF(pyhcmodule);
    
    return 0;
}


/**
 * Calls hexchat_print() to print text to HexChat. Breaks up long strings into 
 * multiple hexchat_print()'s. HexChat seems to get unstable if buffers longer 
 * than 4K are passed to hexchat_print().
 * @param str   - UTF-8 encoded string.
 * @param size  - the size in bytes of the string. 
 */
void
print_string(const char *str, Py_ssize_t size)
{
    static const 
    int     MAX_BUF = 3072;

    char    *buf;
    char    ch;
    int     i       = 0;
    int     j       = 0;
    int     nl_i    = 0;
    int     nl_j    = 0;
    int     alt_i   = 0;
    int     alt_j   = 0;

    if (size <= MAX_BUF) {
        // Size is small enough - just print the string.
        hexchat_print(ph, str);
    }
    else {
        buf = PyMem_RawMalloc(MAX_BUF);

        for ( ; i < size; i++, j++) { 
            ch = str[i];

            buf[j] = ch;

            if (ch == '\n') {
                // Track last newline.
                nl_i = i;
                nl_j = j;
            }
            else if (ch == '.' || ch == ',' || ch == ';' ||
                     ch == ' ' || ch == '\t') {
                // Other suitable characters to break on.
                alt_i = i;
                alt_j = j;
            }
            if (j > MAX_BUF - 4) {
                // Figure out whether to break on '\n' or other char.
                if (nl_j == 0) {
                    if (alt_j) {
                        nl_i = alt_i;
                        nl_j = alt_j;
                    }
                    else {
                        nl_i = i;
                        nl_j = j;
                    }
                }
                buf[nl_j + 1] = '\0';
                buf[nl_j + 2] = '\0';
                
                // Print substring.
                hexchat_print(ph, buf);
                
                // Advance/reset indices.
                i       = nl_i;
                j       = -1;
                nl_j    = 0;
                alt_j   = 0;
            }
        }
        if (j > 0) {
            buf[j-1] = '\0';
            buf[j  ] = '\0';
            
            // Print remaining string.
            hexchat_print(ph, buf);
        }
        PyMem_RawFree(buf);
    }
}
