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

#define MINPY_MODNAME  "MagPy Python"
#define MINPY_MODDESC  "The alternative Python 3 plugin support."
#define MINPY_VER_STR  "0.1"

#define MINPY_MAJOR_VER       0
#define MINPY_MINOR_VER       1
#define MAX_WORD_ARRAY_LEN    32
 
/**
 * HexChat plugin handle.
 */
hexchat_plugin  *ph;

/**
 * The main Python interpeter's thread state for the HexChat main thread.
 */
//PyThreadState   *py_g_main_threadstate = NULL;

/** 
 * CallbackData - Used as userdata for commands/events hooked on behalf of 
 * Python callbacks.
 */
typedef struct {
    PyObject      *callback;
    PyObject      *userdata;
    PyThreadState *threadstate;
    hexchat_hook  *hook;
} CallbackData;

/**
 * Types of hooks/callbacks - used by code that processes events and registers
 * callbacks.
 */
typedef enum {
    CBV_PRNT      = (1 << 0), CBV_SRV      = (1 << 1), 
    CBV_PRNT_ATTR = (1 << 2), CBV_SRV_ATTR = (1 << 3), 
    CBV_CMD       = (1 << 4), CBV_TIMER    = (1 << 5)
} CB_VER;

/** @defgroup forwarddecls
 *  @{
 */
// Python hexchat module commands.
static PyObject *py_command                (PyObject *, PyObject *);
static PyObject *py_prnt                   (PyObject *, PyObject *);
       PyObject *py_emit_print             (PyObject *, PyObject *, PyObject *);
static PyObject *py_emit_print_attrs       (PyObject *, PyObject *, PyObject *);
static PyObject *py_send_modes             (PyObject *, PyObject *, PyObject *);
static PyObject *py_nickcmp                (PyObject *, PyObject *);
static PyObject *py_strip                  (PyObject *, PyObject *, PyObject *);
static PyObject *py_event_attrs_create     (PyObject *, PyObject *);
       PyObject *py_get_info               (PyObject *, PyObject *);
static PyObject *py_get_prefs              (PyObject *, PyObject *, PyObject *);
       PyObject *py_get_listiter           (PyObject *, PyObject *);
       PyObject *py_get_list               (PyObject *, PyObject *);
static PyObject *py_list_fields            (PyObject *, PyObject *);

static PyObject *py_hook_command           (PyObject *, PyObject *, PyObject *);
static PyObject *py_hook_print             (PyObject *, PyObject *, PyObject *);
static PyObject *py_hook_print_attrs       (PyObject *, PyObject *, PyObject *);
static PyObject *py_hook_server            (PyObject *, PyObject *, PyObject *);
static PyObject *py_hook_server_attrs      (PyObject *, PyObject *, PyObject *);
static PyObject *py_hook_timer             (PyObject *, PyObject *, PyObject *);
static PyObject *py_all_hook_inner         (CB_VER, 
                                            PyObject *, PyObject *, PyObject *);
static PyObject *py_unhook                 (PyObject *, PyObject *);
static PyObject *py_hook_unload            (PyObject *, PyObject *);
static PyObject *py_find_context           (PyObject *, PyObject *, PyObject *);
static PyObject *py_get_context            (PyObject *, PyObject *);
static PyObject *py_set_context            (PyObject *, PyObject *);

static PyObject *py_set_pluginpref         (PyObject *, PyObject *);
static PyObject *py_get_pluginpref         (PyObject *, PyObject *);
static PyObject *py_del_pluginpref         (PyObject *, PyObject *);
static PyObject *py_list_pluginpref        (PyObject *, PyObject *);

// Capsule pointer freeing functions.
       void     py_attrs_free_fn           (PyObject *);
//static void     py_list_free_fn            (PyObject *);
static void     py_hook_free_fn            (PyObject *);

// HexChat command callbacks.
static int      mpy_callback               (char *[], char *[], void *);


// HexChat callbacks that wrap Python callbacks...
static int      hc_command_callback        (char *[], char *[], void *);
static int      hc_print_callback          (char *[], void *);
static int      hc_print_attrs_callback    (char *[], 
                                            hexchat_event_attrs *, void *);
static int      hc_server_callback         (char *[], char *[], void *);
static int      hc_server_attrs_callback   (char *[], char *[], 
                                            hexchat_event_attrs *, void *);
static int      hc_timer_callback          (void *);
static int      hc_all_callback_inner      (CB_VER, char *[], char *[], 
                                            hexchat_event_attrs *, void *);

/** @} */ /* end forwarddecls */

/**
 * hexchat_methods - Python hexchat module methods defined in this array.
 */
static PyMethodDef hexchat_methods[] = {
    {"command",      (PyCFunction)py_command,      METH_VARARGS,
     "Executes a command as if typed into HexChat's input box."},

    {"prnt",         (PyCFunction)py_prnt,         METH_VARARGS,
     "Prints message to the active HexChat window."},

    {"emit_print",   (PyCFunction)py_emit_print,   METH_VARARGS | METH_KEYWORDS,
     "Generates a print event with the given arguments."},
     
    {"emit_print_attrs",
                     (PyCFunction)py_emit_print_attrs, 
                                                   METH_VARARGS | METH_KEYWORDS,
     "This is the same as hexchat_emit_print() but it passes an "
     "hexchat_event_attrs to hexchat with the print attributes."},
     
    {"send_modes",   (PyCFunction)py_send_modes,   METH_VARARGS | METH_KEYWORDS,
     "Sends a number of channel mode changes to the current channel."},
     
    {"nickcmp",      (PyCFunction)py_nickcmp,      METH_VARARGS,
     "Performs a nick name comparision based on the current server "
     "connection."},
     
    {"strip",        (PyCFunction)py_strip,        METH_VARARGS | METH_KEYWORDS,
     "Strips mIRC color codes and/or text attributes (bold, underlined etc) "
     "from the given string and returns a newly allocated string."},
     
    {"event_attrs_create",
                     (PyCFunction)py_event_attrs_create,
                                                   METH_NOARGS,
     "Allocates a new hexchat_event_attrs. The attributes are initially marked "
     "as unused."},
     
    {"get_info",     (PyCFunction)py_get_info,     METH_VARARGS,
     "Returns information based on your current context."},

    {"get_prefs",    (PyCFunction)py_get_prefs,    METH_VARARGS | METH_KEYWORDS,
     "Provides HexChat’s setting information (that which is available through "
     "the /SET command)."},
     
    {"get_listiter", (PyCFunction)py_get_listiter, METH_VARARGS,
     "Returns an iterator for the requested list. The iterator provides faster "
     "access to list data since it doesn't internally construct a list before "
     "returning, as does get_list()."},
     
    {"get_list",     (PyCFunction)py_get_list,     METH_VARARGS,
     "Constructs and returns lists of information. List items are "
     "namedtuple's."},
     
    {"list_fields",  (PyCFunction)py_list_fields,  METH_VARARGS,
     "Lists fields in a given list."},
     
    {"hook_command", (PyCFunction)py_hook_command, METH_VARARGS | METH_KEYWORDS,
     "Adds a new /command."},
     
    {"hook_print",   (PyCFunction)py_hook_print,   METH_VARARGS | METH_KEYWORDS,
     "Registers a function to trap any print events."},

    {"hook_print_attrs", 
                     (PyCFunction)py_hook_print_attrs,
                                                   METH_VARARGS | METH_KEYWORDS,
     "Registers a function to trap any print events."},

    {"hook_server",  (PyCFunction)py_hook_server,  METH_VARARGS | METH_KEYWORDS,
     "Registers a function to be called when a certain server event occurs."},
     
    {"hook_server_attrs",
                     (PyCFunction)py_hook_server_attrs,
                                                   METH_VARARGS | METH_KEYWORDS,
    "Registers a function to be called when a certain server event occurs."},
    
    {"hook_timer",   (PyCFunction)py_hook_timer,   METH_VARARGS | METH_KEYWORDS,
     "Registers a function to be called every “timeout” milliseconds."},    

    {"unhook",       (PyCFunction)py_unhook,       METH_VARARGS,
     "Unhooks any hook registered with hexchat_hook_print/server/timer/command." 
    },

    {"hook_unload",  (PyCFunction)py_hook_unload,  METH_VARARGS,
     "Registers a function to be called when the plugin is about to be "
     "unloaded."
    },

    {"find_context", (PyCFunction)py_find_context, METH_VARARGS | METH_KEYWORDS,
     "Finds a context based on a channel and servername." 
    },
    {"get_context",  (PyCFunction)py_get_context,  METH_NOARGS,
     "Returns the current context for your plugin." 
    },    
    {"set_context",  (PyCFunction)py_set_context,  METH_VARARGS,
     "Changes your current context to the one given."},
    {"set_pluginpref", 
                     (PyCFunction)py_set_pluginpref, 
                                                   METH_VARARGS,
     "Saves a plugin-specific setting with string value to a plugin-specific "
     "config file."},
    {"get_pluginpref",
                     (PyCFunction)py_get_pluginpref,
                                                   METH_VARARGS,
     "Loads a plugin-specific setting with string value from a plugin-specific "
     "config file."},
    {"del_pluginpref", 
                     (PyCFunction)py_del_pluginpref,
                                                   METH_VARARGS,
     "Deletes a plugin-specific setting from a plugin-specific config file."},
    {"list_pluginpref",
                     (PyCFunction)py_list_pluginpref, 
                                                   METH_NOARGS,
     "Builds a comma-separated list of the currently saved settings from a "
     "plugin-specific config file."},

    {NULL, NULL, 0, NULL}
};

/**
 * hexchat_module - Python hexchat module defined in this struct.
 */
static struct PyModuleDef hexchat_module = {
    PyModuleDef_HEAD_INIT,
    "hexchat",
    NULL,         // module documentation, may be NULL
    -1,           // size of per-interpreter state of the module.
    hexchat_methods
};

/**
 * PyInit_hexchat - Python hexchat module init function. This gets called when
 * "import hexchat" or similar code is evaluated in a loading Python script.
 */
PyMODINIT_FUNC
PyInit_hexchat(void)
{
    PyObject    *pymodule;
    PyObject    *pyvertuple;
    PyObject    *pyproxy;
    
    // List of types/classes provided by the hexchat module that need to be 
    // registered below.
    const void *types_to_register[] = {
        "AsyncResult",      AsyncResultTypePtr,
        "Context",          ContextTypePtr,
        "Delegate",         DelegateTypePtr,
        "DelegateProxy",    DelegateProxyTypePtr,
        "EventAttrs",       EventAttrsTypePtr,
        "ListIter",         ListIterTypePtr,
        "OutStream",        OutStreamTypePtr,
        NULL, NULL
    };
    
    pymodule = PyModule_Create(&hexchat_module);

    // Priorities for callback registration.
    PyModule_AddIntConstant(pymodule, "PRI_HIGHEST",     HEXCHAT_PRI_HIGHEST );
    PyModule_AddIntConstant(pymodule, "PRI_HIGH",        HEXCHAT_PRI_HIGH    );
    PyModule_AddIntConstant(pymodule, "PRI_NORM",        HEXCHAT_PRI_NORM    );
    PyModule_AddIntConstant(pymodule, "PRI_LOW",         HEXCHAT_PRI_LOW     );
    PyModule_AddIntConstant(pymodule, "PRI_LOWEST",      HEXCHAT_PRI_LOWEST  );

    // Callback return values.
    PyModule_AddIntConstant(pymodule, "EAT_NONE",        HEXCHAT_EAT_NONE    );
    PyModule_AddIntConstant(pymodule, "EAT_HEXCHAT",     HEXCHAT_EAT_HEXCHAT );
    PyModule_AddIntConstant(pymodule, "EAT_PLUGIN",      HEXCHAT_EAT_PLUGIN  );
    PyModule_AddIntConstant(pymodule, "EAT_ALL",         HEXCHAT_EAT_ALL     );
    
    // File descriptor consts.
    PyModule_AddIntConstant(pymodule, "FD_READ",         HEXCHAT_FD_READ     );
    PyModule_AddIntConstant(pymodule, "FD_WRITE",        HEXCHAT_FD_WRITE    );
    PyModule_AddIntConstant(pymodule, "FD_EXCEPTION",    HEXCHAT_FD_EXCEPTION);
    PyModule_AddIntConstant(pymodule, "FD_NOTSOCKET",    HEXCHAT_FD_NOTSOCKET);
    
    // Channel flags.
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_CONNECTED",           0x0001);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_CONNECTING",          0x0002);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_MARKED_AWAY",         0x0004);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_END_OF_MOTD",         0x0008);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_HAS_WHOX",            0x0010);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_HAS_IDMSG",           0x0020);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_HIDE_JOIN",           0x0040);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_HIDE_JOIN_UNSET",     0x0080);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_BEEP_ON_MESSAGE",     0x0100);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_BLINK_TRAY",          0x0200);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_BLINK_TASKBAR",       0x0400);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_LOGGING",             0x0800);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_LOGGING_UNSET",       0x1000);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_SCROLLBACK",          0x2000);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_SCROLLBACK_UNSET",    0x4000);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_STRIP_COLORS",        0x8000);
    PyModule_AddIntConstant(pymodule, "CHAN_FLAG_STRIP_COLORS_UNSET",  
                                                                      0x10000);    
    // Channel types.
    PyModule_AddIntConstant(pymodule, "CHAN_TYPE_SERVER",                   1);
    PyModule_AddIntConstant(pymodule, "CHAN_TYPE_CHANNEL",                  2);
    PyModule_AddIntConstant(pymodule, "CHAN_TYPE_DIALOG",                   3);
    PyModule_AddIntConstant(pymodule, "CHAN_TYPE_NOTICE",                   4);
    PyModule_AddIntConstant(pymodule, "CHAN_TYPE_SNOTICE",                  5);    
    
    // DCC status values.
    PyModule_AddIntConstant(pymodule, "DCC_STATUS_QUEUED",                  0);
    PyModule_AddIntConstant(pymodule, "DCC_STATUS_ACTIVE",                  1);
    PyModule_AddIntConstant(pymodule, "DCC_STATUS_FAILED",                  2);
    PyModule_AddIntConstant(pymodule, "DCC_STATUS_DONE",                    3);
    PyModule_AddIntConstant(pymodule, "DCC_STATUS_CONNECTING",              4);
    PyModule_AddIntConstant(pymodule, "DCC_STATUS_ABORTED",                 5);
    
    // Table online has these listed with values 0, 1, 1, 1 which can't be
    // right. So I made them sequential ints.
    PyModule_AddIntConstant(pymodule, "DCC_TYPE_SEND",                      0);
    PyModule_AddIntConstant(pymodule, "DCC_TYPE_RECIEVE",                   1);
    PyModule_AddIntConstant(pymodule, "DCC_TYPE_CHATRECV",                  2);
    PyModule_AddIntConstant(pymodule, "DCC_TYPE_CHATSEND",                  3);
    
    // The table online has these "flags" listed as sequential ints.
    // I need to verify whether the online page is wrong, or my understanding
    // of what "flags" means wrt HexChat is wrong.
    PyModule_AddIntConstant(pymodule, "IGN_FLAG_PRIVATE",                0x01);
    PyModule_AddIntConstant(pymodule, "IGN_FLAG_NOTICE",                 0x02);
    PyModule_AddIntConstant(pymodule, "IGN_FLAG_CHANNEL",                0x04);
    PyModule_AddIntConstant(pymodule, "IGN_FLAG_CTCP",                   0x08);
    PyModule_AddIntConstant(pymodule, "IGN_FLAG_INVITE",                 0x10);
    PyModule_AddIntConstant(pymodule, "IGN_FLAG_UNIGNORE",               0x20);
    PyModule_AddIntConstant(pymodule, "IGN_FLAG_NOSAVE",                 0x40);
    PyModule_AddIntConstant(pymodule, "IGN_FLAG_DCC",                    0x80);
    
    // IRC color codes. Use these in strings printed to he/xchat.
    PyModule_AddStringConstant(pymodule, "IRC_WHITE",                "\00300");
    PyModule_AddStringConstant(pymodule, "IRC_BLACK",                "\00301");
    PyModule_AddStringConstant(pymodule, "IRC_NAVY",                 "\00302");
    PyModule_AddStringConstant(pymodule, "IRC_GREEN",                "\00303");
    PyModule_AddStringConstant(pymodule, "IRC_RED",                  "\00304");
    PyModule_AddStringConstant(pymodule, "IRC_MAROON",               "\00305");
    PyModule_AddStringConstant(pymodule, "IRC_PURPLE",               "\00306");
    PyModule_AddStringConstant(pymodule, "IRC_OLIVE",                "\00307");
    PyModule_AddStringConstant(pymodule, "IRC_YELLOW",               "\00308");
    PyModule_AddStringConstant(pymodule, "IRC_LIGHT_GREEN",          "\00309");
    PyModule_AddStringConstant(pymodule, "IRC_TEAL",                 "\00310");
    PyModule_AddStringConstant(pymodule, "IRC_CYAN",                 "\00311");
    PyModule_AddStringConstant(pymodule, "IRC_ROYAL_BLUE",           "\00312");
    PyModule_AddStringConstant(pymodule, "IRC_MAGENTA",              "\00313");
    PyModule_AddStringConstant(pymodule, "IRC_GRAY",                 "\00314");
    PyModule_AddStringConstant(pymodule, "IRC_LIGHT_GRAY",           "\00315");
    
    // IRC text format codes. Use these in strings printed to he/xchat.
    PyModule_AddStringConstant(pymodule, "IRC_BOLD",                   "\002");
    PyModule_AddStringConstant(pymodule, "IRC_HIDDEN",                 "\010");
    PyModule_AddStringConstant(pymodule, "IRC_UNDERLINE",              "\037");
    PyModule_AddStringConstant(pymodule, "IRC_ORIG_ATTRIBS",           "\017");
    PyModule_AddStringConstant(pymodule, "IRC_REVERSE_COLOR",          "\026");
    PyModule_AddStringConstant(pymodule, "IRC_BEEP",                   "\007");
    PyModule_AddStringConstant(pymodule, "IRC_ITALICS",                "\035");     
    
    pyvertuple = Py_BuildValue("(ii)", MINPY_MAJOR_VER, MINPY_MINOR_VER);

    // [Python>>] __version__ = (<major>, <minor>,)
    PyModule_AddObject(pymodule, "__version__", pyvertuple);

    // Register the classes/types provided by the hexchat module. 

    for (int i = 0; types_to_register[i] != NULL; i += 2) {
        if (PyType_Ready((PyTypeObject *)types_to_register[i + 1]) < 0) {
            hexchat_printf(ph,
                "\0034Error encountered registering hexchat.%s.",
                (char *)types_to_register[i]);
            return NULL;
        }
        PyModule_AddObject(pymodule,
                           (char *)types_to_register[i],
                           (PyObject *)types_to_register[i + 1]);
    }

    // Add DelegateProxy's for the module API.
    pyproxy = PyObject_CallFunction((PyObject *)DelegateProxyTypePtr,
                                    "Oi", pymodule, false);
    PyModule_AddObject(pymodule, "synchronous", pyproxy);

    pyproxy = PyObject_CallFunction((PyObject *)DelegateProxyTypePtr,
                                    "Oi", pymodule, true);
    PyModule_AddObject(pymodule, "asynchronous", pyproxy);

    return pymodule;
}

/**
* hexchat_plugin_get_info - C-facing exported plugin info function.
*/
void
hexchat_plugin_get_info(char **name, char **desc, char **version,
    void **reserved)
{
    *name      = MINPY_MODNAME;
    *desc      = MINPY_MODDESC;
    *version = MINPY_VER_STR;
}

/**
* hexchat_plugin_init - C-facing exported initialization function for the
* plugin.
*/
int hexchat_plugin_init(hexchat_plugin *plugin_handle, char **plugin_name,
    char **plugin_desc, char **plugin_version, char *arg)
{
    ph = plugin_handle;

    wchar_t *argv[] = { L"<hexchat>", NULL };

    //hexchat_plugin_get_info(plugin_name, plugin_desc, plugin_version, NULL);
    *plugin_name     = MINPY_MODNAME;
    *plugin_desc     = MINPY_MODDESC;
    *plugin_version = MINPY_VER_STR;

    hexchat_hook_command(ph, "MPY", HEXCHAT_PRI_NORM, mpy_callback,
                         "MinPython commands.", NULL);

    hexchat_printf(ph, "%s loaded.", MINPY_MODNAME);

    PyImport_AppendInittab("hexchat", PyInit_hexchat);

    Py_Initialize();
    PySys_SetArgv(1, argv);

    PyEval_InitThreads();

    // Set up stdout/stderr for the main interpreter.
    interp_set_up_stdout_stderr();
    
    // Store the main interpreter's thread state.
    py_g_main_threadstate = PyEval_SaveThread();

    // Initialize the plugins module.
    init_plugins();

    // Create the global console interpreter.
    create_console_interp();

    return 1;
}

/**
* hexchat_plugin_deinit - C-facing exported de-initialization function for
* the plugin.
*/
int
hexchat_plugin_deinit(hexchat_plugin *plugin_handle)
{
    int ret;

    close_console();
    delete_console_interp();
    delete_plugins();

    switch_threadstate(py_g_main_threadstate);

    ret = Py_FinalizeEx();

    hexchat_printf(ph, "%s unloaded (%i).", MINPY_MODNAME, ret);

    return 1; /* return 1 for success */
}

/**
 * Implements the hexchat.command() function.
 */
PyObject *
py_command(PyObject *self, PyObject *args)
{
    PyObject *pycommand;

    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "U:command", &pycommand)) {
        return NULL;
    }
    hexchat_command(ph, PyUnicode_AsUTF8(pycommand));
    Py_RETURN_NONE;
}

/**
 * Implements the hexchat.prnt() function.
 */
PyObject *
py_prnt(PyObject *self, PyObject *args)
{
    PyObject   *pymsg;

    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "U:prnt", &pymsg)) {
        return NULL;
    }
    hexchat_print(ph, PyUnicode_AsUTF8(pymsg));
    Py_RETURN_NONE;
}

/**
 * Implements the hexchat.emit_print() function.
 */
PyObject *
py_emit_print(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int         retval;
    PyObject    *pyevent_name;
    PyObject    *pyarg[6] = { NULL, NULL, NULL, NULL, NULL, NULL };
    const char  *arg[6]   = { NULL, NULL, NULL, NULL, NULL, NULL };
    
    static char *keywords[] = { "event_name", "arg1", "arg2", "arg3",
                                "arg4", "arg5", "arg6", NULL };

    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "U|UUUUUU:emit_print",
                                     keywords, &pyevent_name, &pyarg[0], 
                                     &pyarg[1], &pyarg[2], &pyarg[3], 
                                     &pyarg[4], &pyarg[5])) {
        return NULL;
    }
    for (int i = 0; i < 6 && pyarg[i]; i++) {
        arg[i] = PyUnicode_AsUTF8(pyarg[i]);
    }
    retval = hexchat_emit_print(ph, 
                                PyUnicode_AsUTF8(pyevent_name), 
                                arg[0], arg[1], arg[2], arg[3], arg[4], arg[5],
                                NULL);

    return PyLong_FromLong(retval);
}

/**
 * Implements the hexchat.emit_print_attrs() function.
 */
PyObject *
py_emit_print_attrs(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int                 retval;
    PyObject            *pyattrs;
    PyObject            *pytime;
    hexchat_event_attrs attrs;
    PyObject            *pyevent_name;
    PyObject            *pyarg[6] = { NULL, NULL, NULL, NULL, NULL, NULL };
    const char          *arg[6]   = { NULL, NULL, NULL, NULL, NULL, NULL };

    static char         *keywords[] = { "event_name", "attrs" "arg1", 
                                        "arg2", "arg3", "arg4", "arg5", "arg6", 
                                        NULL };

    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTupleAndKeywords(args, kwargs,
                                     "UO|UUUUUU:emit_print_attrs", 
                                     keywords, &pyevent_name, &pyattrs, 
                                     &pyarg[0], &pyarg[1], &pyarg[2], 
                                     &pyarg[3], &pyarg[4], &pyarg[5])) {
        return NULL;
    }
    if (!PyObject_IsInstance(pyattrs, (PyObject *)EventAttrsTypePtr)) {
        PyErr_SetString(PyExc_TypeError,
                        "attrs argument must be an instance of EventAttrs.");
        return NULL;
    }
    pytime                = PyObject_GetAttrString(pyattrs, "server_time_utc");
    attrs.server_time_utc = (time_t)PyLong_AsLongLong(pytime);

    Py_DECREF(pytime);

    for (int i = 0; i < 6 && pyarg[i]; i++) {
        arg[i] = PyUnicode_AsUTF8(pyarg[i]);
    }

    retval = hexchat_emit_print_attrs(ph, &attrs, 
                                      PyUnicode_AsUTF8(pyevent_name), 
                                      arg[0], arg[1], arg[2], arg[3], arg[4], 
                                      arg[5], NULL);
    return PyLong_FromLong(retval);    
}

/**
 * Implements the hexchat.send_modes() function.
 */
PyObject *
py_send_modes(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject    *pytargets;
    char        *targets[MAX_WORD_ARRAY_LEN]; // TODO - another const?
    Py_ssize_t  ntargets;
    int         modes_per_line;
    int         sign, mode;
    PyObject    *pytarget;
    static char *keywords[] = { "targets", "modes_per_line", "sign", "mode", 
                                NULL };
    
    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OiCC:send_modes",
                                     keywords, &pytargets, &modes_per_line, 
                                     &sign, &mode)) {
        return NULL;
    }
    ntargets = PySequence_Length(pytargets);
    if (ntargets == -1) {
        return NULL;
    }
    // TODO - Check length and set error if too large.
    for (int i = 0; i < MAX_WORD_ARRAY_LEN && i < ntargets; i++) {
        pytarget = PySequence_GetItem(pytargets, i); // NR.
        if (!pytarget) {
            return NULL;
        }
        targets[i] = PyUnicode_AsUTF8(pytarget);
        if (!targets[i]) {
            return NULL;
        }
        Py_DECREF(pytarget);
    }
    hexchat_send_modes(ph, (const char **)targets, (int)ntargets, modes_per_line, 
                       (char)sign, (char)mode);
    Py_RETURN_NONE;
}

/**
 * Implements the hexchat.nickcmp() function.
 */
PyObject *
py_nickcmp(PyObject *self, PyObject *args)
{
    int         retval;
    PyObject    *s1, *s2;

    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "UU:nickcmp", &s1, &s2)) {
        return NULL;
    }    
    retval = hexchat_nickcmp(ph, PyUnicode_AsUTF8(s1), PyUnicode_AsUTF8(s2));

    return PyLong_FromLong(retval);
}

/**
 * Implemets the hexchat.strip() function.
 */
PyObject *
py_strip(PyObject *self, PyObject *args, PyObject *kwargs)
{
    char        *retval;
    PyObject    *pyretval;
    PyObject    *pytext;
    int         len         = -1;
    int         flags       =  3;
    static char *keywords[] = { "text", "len", "flags", NULL };

    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "U|ii:strip",
                                     keywords, &pytext, &len, &flags)) {
        return NULL;
    }    
    retval    = hexchat_strip(ph, PyUnicode_AsUTF8(pytext), len, flags);
    pyretval = PyUnicode_FromString(retval);
    hexchat_free(ph, retval);
    
    return pyretval;
}

/**
 * Implements the hexchat.event_attrs_create() function.
 */
PyObject *
py_event_attrs_create(PyObject *self, PyObject *Py_UNUSED(args))
{
    PyObject *pyattrs;
    if (main_thread_check()) {
        return NULL;
    }
    pyattrs = PyObject_CallFunction((PyObject *)EventAttrsTypePtr, NULL);
    if (!pyattrs) {
        return NULL;
    }
    return pyattrs;
}

/** 
 * Implements the hexchat.get_info() function.
 */
PyObject *
py_get_info(PyObject *self, PyObject *args)
{
    const char  *retval;
    PyObject    *pyretval = NULL;
    PyObject    *pyid;
    PyObject    *pybytes;

    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "U:get_info", &pyid)) {
        return NULL;
    }
    retval = hexchat_get_info(ph, PyUnicode_AsUTF8(pyid));

    if (!PyUnicode_CompareWithASCIIString(pyid, "gtkwin_ptr") ||
        !PyUnicode_CompareWithASCIIString(pyid, "win_ptr")) {

        pyretval = PyLong_FromVoidPtr((void *)retval);
    } 
    else if (retval) {
        pybytes  = PyBytes_FromString(retval);
        pyretval = PyUnicode_FromEncodedObject(pybytes, "utf-8", "replace");

        Py_DECREF(pybytes);
    }
    else {
        PyErr_SetString(PyExc_KeyError, "Bad info name.");
    } 
    return pyretval;
}

/**
 * Implements the hexchat.get_prefs() function.
 */
PyObject *
py_get_prefs(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int         retval;
    PyObject    *pyretval = Py_None;
    PyObject    *pyname;
    PyObject    *pybytes;
    const char  *text;
    int         integer;
    static char *keywords[] = { "name", NULL };
    
    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "U:get_prefs", 
                                     keywords, &pyname)) {
        return NULL;
    }    
    retval = hexchat_get_prefs(ph, PyUnicode_AsUTF8(pyname), &text, &integer);
    
    switch(retval)
    {
        case 0: // Failed.
            PyErr_SetString(PyExc_KeyError, "Bad prefs name.");
            return NULL;
        case 1: // Return a string.
            pybytes  = PyBytes_FromString(text);
            pyretval = PyUnicode_FromEncodedObject(pybytes, "utf-8", "replace");
            Py_DECREF(pybytes);
            break;
        case 2: // Return an integer.
            pyretval = PyLong_FromLong(integer);
            break;
        case 3: // Return a boolean.
            pyretval = integer ? Py_True : Py_False;
            Py_INCREF(pyretval);
            break;
        default: // Shouldn't get here unless there's a bug in hexchat.
            PyErr_Format(PyExc_RuntimeError, 
                         "hexchat_get_prefs() returned invalid value (%d) "
                         "in %s() FILE: %s.",
                         retval, __func__, __FILE__);
            pyretval = NULL;
            break;
    }
    return pyretval;
}

/**
 * Implements the hexchat.get_listiter() function.
 */
PyObject *
py_get_listiter(PyObject *self, PyObject *args)
{
    PyObject        *pyobj;
    PyObject        *pyname;

    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "U:get_listiter", &pyname)) {
        return NULL;
    }
    pyobj = PyObject_CallFunction((PyObject *)ListIterTypePtr, "O", pyname);
    return pyobj;
}

/**
 * Implements the hexchat.get_list() function. Although this iterates over a
 * list and constructs a Python list for all data before returning, it's 
 * still very fast.
 */
PyObject *
py_get_list(PyObject *self, PyObject *args)
{   
    PyObject    *pyiter;
    PyObject    *pyname;
    PyObject    *pyconstr;
    PyObject    *pyname_tupl;
    PyObject    *pyattr;
    PyObject    *pyitem;
    PyObject    *pyntup_type;
    PyObject    *pyntup;
    PyObject    *pyargs;
    PyObject    *pylist;
    Py_ssize_t  len;
    
    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "U:get_list", &pyname)) {
        return NULL;
    }
    
    // Get the iterator for the requested list.
    pyiter = PyObject_CallFunction((PyObject *)ListIterTypePtr, "O", pyname);

    if (!pyiter) {
        return NULL;
    }
    // Grab the namedtuple constructor.
    pyconstr = interp_get_namedtuple_constr(); // BR.

    // Get the list information. List name, field names, etc.
    pyname       = PyUnicode_FromFormat("%U_item", pyname);
    pyname_tupl  = PyObject_GetAttrString(pyiter, "field_names");
    len          = PyObject_Length(pyname_tupl);

    // Create a custom namedtuple type to use as list items.
    pyntup_type  = PyObject_CallFunction(pyconstr, "OO", pyname, pyname_tupl);

    Py_DECREF(pyname);

    pylist = PyList_New(0);

    // Iterate over the list.
    while ((pyitem = PyIter_Next(pyiter))) {
        
        // Build arguments for the nametuple type constructor.
        pyargs = PyTuple_New(len);

        for (Py_ssize_t i = 0; i < len; i++) {
            pyname = PyTuple_GetItem(pyname_tupl, i);   // BR.
            pyattr = PyObject_GetAttr(pyitem, pyname);  // NR.
            PyTuple_SetItem(pyargs, i, pyattr);         // Steals ref.
        }
        // Create the custom namedtuple.
        pyntup = PyObject_CallObject(pyntup_type, pyargs);

        // Append it to the list.
        PyList_Append(pylist, pyntup);

        Py_DECREF(pyitem);
        Py_DECREF(pyargs);
        Py_DECREF(pyntup);
    }
    
    Py_DECREF(pyiter);
    Py_DECREF(pyname_tupl);
    Py_DECREF(pyntup_type);

    return pylist;
}

/**
 * Implements the hexchat.list_fields() function.
 */
PyObject *
py_list_fields(PyObject *self, PyObject *args)
{
    const char * const  *retval;
    PyObject            *pyretval;
    PyObject            *pyname;
    PyObject            *pystr;
    PyObject            *pybytes;
    
    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "U:list_fields", &pyname)) {
        return NULL;
    }    
    retval = hexchat_list_fields(ph, PyUnicode_AsUTF8(pyname));
    if (!retval) {
        PyErr_SetString(PyExc_KeyError, "Bad list name.");
        return NULL;
    }
    pyretval = PyList_New(0);
    for (int i = 0; retval[i] != NULL; i++) {
        pybytes = PyBytes_FromString(retval[i]);
        pystr   = PyUnicode_FromEncodedObject(pybytes, "utf-8", "replace");
        PyList_Append(pyretval, pystr);
        Py_DECREF(pybytes);
        Py_DECREF(pystr);
    }
    return pyretval;
}

/**
 * Registers the given callback for HexChat events. All hook_xxx functions
 * invoke this internally.
 */
PyObject *
py_all_hook_inner(CB_VER ver, PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject        *pyname     = NULL;
    int             priority    = HEXCHAT_PRI_NORM;
    PyObject        *pycallback;
    PyObject        *pyhelp     = NULL;
    PyObject        *pyuserdata = Py_None;
    const char      *name       = NULL;
    const char      *help       = NULL;
    hexchat_hook    *hook       = NULL;
    PyObject        *pyhook;
    CallbackData    *userdata;
    int             timeout;
    const char      *cmd_spec   = NULL;

    static char     *cmd_kwds[] = { "name", "callback", "userdata",
                                    "priority", "help", NULL };
    static char     *prt_kwds[] = { "name", "callback", "userdata",
                                    "priority", NULL };
    static char     *tmr_kwds[] = { "timeout", "callback", "userdata", NULL };

    if (!(ver & CBV_TIMER) && main_thread_check()) {
        return NULL;
    }

    // Parse the function parameters according to callback version.
    
    if (ver & CBV_CMD) {
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "UO|OiU:hook_command",
                                         cmd_kwds, &pyname, &pycallback,
                                         &pyuserdata, &priority, &pyhelp)) {
            return NULL;
        }
    }
    else if (ver & (CBV_PRNT | CBV_PRNT_ATTR | CBV_SRV | CBV_SRV_ATTR)) {
        switch (ver) {
        case CBV_PRNT     : cmd_spec = "UO|Oi:hook_print";        break;
        case CBV_PRNT_ATTR: cmd_spec = "UO|Oi:hook_print_attrs";  break;
        case CBV_SRV      : cmd_spec = "UO|Oi:hook_server";       break;
        case CBV_SRV_ATTR : cmd_spec = "UO|Oi:hook_server_attrs"; break;
        default:
            // Shouldn't ever get here.
            assert("Bad value for ver!" == 0); 
            break;
        }
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, cmd_spec,
                                         prt_kwds, &pyname, &pycallback,
                                         &pyuserdata, &priority)) {
            return NULL;
        }
    }
    else { // ver is CBV_TIMER.
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "iO|O:hook_timer",
                                         tmr_kwds, &timeout, &pycallback, 
                                         &pyuserdata)) {
            return NULL;
        } 
    }
    name = (pyname) ? PyUnicode_AsUTF8(pyname) : "timer";

    if (!PyCallable_Check(pycallback)) {
        PyErr_SetString(PyExc_TypeError, "callback argument must be callable.");
        return NULL;
    }
    
    // Set up userdata for the callback.
    userdata = (CallbackData *)PyMem_RawMalloc(sizeof(CallbackData));

    userdata->callback    = pycallback;
    userdata->userdata    = pyuserdata;
    userdata->threadstate = interp_get_main_threadstate();

    Py_INCREF(pycallback);
    Py_INCREF(pyuserdata);

    // Hook the callback up to the requested event with the userdata.
    switch (ver) {
    case CBV_CMD:
        help = (pyhelp) ? PyUnicode_AsUTF8(pyhelp) : NULL;
        hook = hexchat_hook_command(ph, name, priority, hc_command_callback,
                                    help, userdata);
        break;
    case CBV_PRNT:
        hook = hexchat_hook_print(  ph, name, priority, hc_print_callback, 
                                    userdata);
        break;
    case CBV_PRNT_ATTR:
        hook = hexchat_hook_print_attrs(
                                    ph, name, priority, 
                                    hc_print_attrs_callback, userdata);
        break;
    case CBV_SRV:
        hook = hexchat_hook_server( ph, name, priority, hc_server_callback, 
                                    userdata);
        break;
    case CBV_SRV_ATTR:
        hook = hexchat_hook_server_attrs(
                                    ph, name, priority, 
                                    hc_server_attrs_callback, userdata);
        break;
    case CBV_TIMER:
        hook = hexchat_hook_timer(  ph, timeout, hc_timer_callback, userdata);
        break;
    default: 
        // Can't get here.
        assert("Bad value for ver!" == 0);
        break;
    }
    if (!hook) {
        PyErr_Format(PyExc_RuntimeError, "Unable to set callback for %s.",
                     name);
        return NULL;
    }
    // Get the hook, and create a capsule for it.
    userdata->hook = hook;

    pyhook = PyCapsule_New(hook, "hook", py_hook_free_fn);
    PyCapsule_SetContext(pyhook, userdata);

    // Add hook to the current (sub)interpeter's list of hooks.
    interp_add_hook(pyhook);

    return pyhook;
}

/**
 * py_hook_command - Python-facing version of hook_command().
 */
PyObject *
py_hook_command(PyObject *self, PyObject *args, PyObject *kwargs)
{
    return py_all_hook_inner(CBV_CMD, self, args, kwargs);
}

/**
 * Python facing hook command for hook_print().
 */
PyObject *
py_hook_print(PyObject *self, PyObject *args, PyObject *kwargs)
{
    return py_all_hook_inner(CBV_PRNT, self, args, kwargs);
}

/**
 * Python facing hook command for hook_print_attrs().
 */
PyObject *
py_hook_print_attrs(PyObject *self, PyObject *args, PyObject *kwargs)
{
    return py_all_hook_inner(CBV_PRNT_ATTR, self, args, kwargs);
}

/**
 * Python facing hook command for hook_server().
 */
PyObject *
py_hook_server(PyObject *self, PyObject *args, PyObject *kwargs)
{
    return py_all_hook_inner(CBV_SRV, self, args, kwargs);
}

/**
 * Python facing command for hook_server_attrs().
 */
PyObject *
py_hook_server_attrs(PyObject *self, PyObject *args, PyObject *kwargs)
{
    return py_all_hook_inner(CBV_SRV_ATTR, self, args, kwargs);
}

/**
 * Python facing command for hook_timer().
 */
PyObject *
py_hook_timer(PyObject *self, PyObject *args, PyObject *kwargs)
{
    return py_all_hook_inner(CBV_TIMER, self, args, kwargs);
}

/**
 * Python facing implementation for unhook().
 */
PyObject *
py_unhook(PyObject *self, PyObject *args)
{
    hexchat_hook    *hook;
    PyObject        *pyhook;
    //PyObject        *pyuserdata = Py_None;
    CallbackData    *data;
    PyObject        *pyhook_list;
    PyObject        *pyret;
    const char      *hook_type;
    
    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "O:unhook", &pyhook)) {
        return NULL;
    }
    if (!PyCapsule_CheckExact(pyhook)) {
        PyErr_SetString(PyExc_TypeError, "Must pass a hook to unhook().");
        return NULL;
    }
    
    hook_type = PyCapsule_GetName(pyhook);
    
    if (!strcmp(hook_type, "unload_hook")) {
        // This is an unload hook. Different handling.
        return interp_unhook_unload(pyhook);
    }
    
    hook = (hexchat_hook *)PyCapsule_GetPointer(pyhook, "hook");
    if (!hook) {
        return NULL;
    }
    data  = (CallbackData *)PyCapsule_GetContext(pyhook);

    if (data->hook) {
        hexchat_unhook(ph, hook);
        data->hook  = NULL;
        
        pyhook_list = interp_get_hooks();
        
        pyret = PyObject_CallMethod(pyhook_list, "remove", "O", pyhook);
        
        if (pyret) {
            Py_DECREF(pyret);
        }
        else {
            PyErr_Clear();
        }
    }
    Py_INCREF(data->userdata);
    
    return data->userdata;
}

PyObject *
py_hook_unload(PyObject *self, PyObject *args)
{
    PyObject *pycallback;
    PyObject *pyuserdata  = Py_None;
    PyObject *pyret;
    
    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "O|O:hook_unload", &pycallback, &pyuserdata)) {
        return NULL;
    }
    
    pyret = interp_hook_unload(pycallback, pyuserdata); // NR.
    
    return pyret;
}

// TODO - Make tests that verify corner cases on parameters. For instance,
//        make sure methods will take None if expected to for certain
//        parameters.

/**
 * Python implementation for hexchat.find_context().
 */
PyObject *
py_find_context(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject        *pyserver   = Py_None;
    PyObject        *pychannel  = Py_None;
    PyObject        *pyret;

    static char     *keywords[] = { "server", "channel", NULL };
    
    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|UU:find_context",
                                     keywords, &pyserver, &pychannel)) {
        return NULL;
    }

    pyret = PyObject_CallFunction((PyObject *)
                                  ContextTypePtr, "OOO", 
                                  pyserver, pychannel, Py_None);
    if (!pyret) {
        PyErr_Clear();
        pyret = Py_None;
        Py_INCREF(pyret);
    }
    return pyret;
}

// TODO - Make sure the error output for each function is right - and
//        not for any internal function.

/**
 * 
 */
PyObject *
py_get_context(PyObject *self, PyObject *Py_UNUSED(args))
{   
    PyObject        *pyret;

    if (main_thread_check()) {
        return NULL;
    }
    pyret = PyObject_CallFunction((PyObject *)ContextTypePtr, NULL);

    return pyret;
}

PyObject *
py_set_context(PyObject *self, PyObject *args)
{
    PyObject        *pyctx;
    PyObject        *pyret;
    
    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "O:set_context", &pyctx)) {
        return NULL;
    }
    if (!PyObject_IsInstance(pyctx, (PyObject *)ContextTypePtr)) {
        PyErr_SetString(PyExc_TypeError,
                        "Argument must be a Context object.");
    }
    pyret = PyObject_CallMethod(pyctx, "set", NULL);
    
    return pyret;
}

PyObject *
py_set_pluginpref(PyObject *self, PyObject *args)
{
    #define     MAX_STR 2048
    PyObject    *pyname;
    PyObject    *pykey;
    PyObject    *pyplugin;
    PyObject    *pyvalue;
    PyObject    *pyret;
    const char  *val;
    Py_ssize_t  size;
    int         ret;
    
    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "UO:set_pluginpref", &pyname, &pyvalue)) {
        return NULL;
    }
    // Prepend the plugin name to the key used to store the value.
    pyplugin = interp_get_plugin_name(); // NR.
    pykey    = PyUnicode_FromFormat("%U %U", pyplugin, pyname);

    Py_DECREF(pyplugin);

    if (PyLong_Check(pyvalue)) {
        ret = hexchat_pluginpref_set_int(ph,
                                         PyUnicode_AsUTF8(pykey), 
                                         (int)PyLong_AsLong(pyvalue));
    }
    else if (PyUnicode_Check(pyvalue)) {
        val = PyUnicode_AsUTF8AndSize(pyvalue, &size);
        if (size >= MAX_STR - 1) {
            PyErr_Format(PyExc_RuntimeError,
                         "String passed to set_pluginpref() exceeds "
                         "maximum encoded bytes length (%i).", MAX_STR);
            Py_DECREF(pykey);
            return NULL;
        }
        ret = hexchat_pluginpref_set_str(ph,
                                         PyUnicode_AsUTF8(pykey),
                                         val);
    }
    else {
        PyErr_SetString(PyExc_TypeError,
                        "pluginpref value must be a string or integer value.");
        Py_DECREF(pykey);
        return NULL;
    }
    Py_DECREF(pykey);
    pyret = (ret) ? Py_True : Py_False;
    Py_INCREF(pyret);
    return pyret;
}

PyObject *
py_get_pluginpref(PyObject *self, PyObject *args)
{
    #define     MAX_STR 2048
    PyObject    *pyname;
    PyObject    *pyplugin;
    PyObject    *pykey;
    PyObject    *pyret;
    char        val[MAX_STR];
    int         ret;
    
    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "U:get_pluginpref", &pyname)) {
        return NULL;
    }
    // Prepend the plugin name to the key used to retrieve the value.
    pyplugin = interp_get_plugin_name(); // NR.
    pykey    = PyUnicode_FromFormat("%U %U", pyplugin, pyname);

    Py_DECREF(pyplugin);

    ret = hexchat_pluginpref_get_int(ph, PyUnicode_AsUTF8(pykey));
    
    if (ret == -1) {
        ret = hexchat_pluginpref_get_str(ph, PyUnicode_AsUTF8(pykey), val);
        
        if (ret) {
            pyret = PyUnicode_FromString(val);
        }
        else {
            pyret = Py_None;
            Py_INCREF(pyret);
        }
    }
    else {
        pyret = PyLong_FromLong(ret);
    }
    Py_DECREF(pykey);
    return pyret;
}

PyObject *
py_del_pluginpref(PyObject *self, PyObject *args)
{
    PyObject *pyname;
    PyObject *pyplugin;
    PyObject *pykey;
    PyObject *pyret;
    int      ret;

    if (main_thread_check()) {
        return NULL;
    }
    if (!PyArg_ParseTuple(args, "U:del_pluginpref", &pyname)) {
        return NULL;
    }
    // Prepend plugin name to key.
    pyplugin = interp_get_plugin_name(); // NR.
    pykey    = PyUnicode_FromFormat("%U %U", pyplugin, pyname);

    ret   = hexchat_pluginpref_delete(ph, PyUnicode_AsUTF8(pykey));
    pyret = (ret) ? Py_True : Py_False;

    Py_DECREF(pyplugin);
    Py_DECREF(pykey);
    Py_INCREF(pyret);

    return pyret;
}

PyObject *
py_list_pluginpref(PyObject *self, PyObject *Py_UNUSED(args))
{
    #define     LIST_SIZE 4096
    char        dest[LIST_SIZE];
    int         ret;
    PyObject    *pyplugin;
    PyObject    *pykey;
    PyObject    *pystr;
    PyObject    *pyret;
    PyObject    *pysep;
    PyObject    *pylst;
    PyObject    *pyemp;
    PyObject    *pybool;

    Py_ssize_t  size;

    ret = hexchat_pluginpref_list(ph, dest);

    if (ret) {
        pyplugin = interp_get_plugin_name(); // NR.
        pykey    = PyUnicode_FromFormat("%U ", pyplugin);

        // Get the prefs string from hexchat, then split it on ','.
        pysep = PyUnicode_FromString(",");
        pystr = PyUnicode_FromString(dest);
        pylst = PyUnicode_Split(pystr, pysep, -1);
        Py_DECREF(pystr);
        Py_DECREF(pysep);

        size  = PyObject_Length(pylst);
        pyret = PyList_GetSlice(pylst, 0, size - 1);

        Py_DECREF(pylst);
        
        pylst = pyret;
        pyret = PyList_New(0);
        pyemp = PyUnicode_FromString("");

        // Build a list specific to the plugin by matching the first part of
        // the var name to the plugin name.
        for (Py_ssize_t i = 0; i < size - 1; i++) {
            pystr  = PyList_GetItem(pylst, i); // BR.
            pybool = PyObject_CallMethod(pystr, "startswith", "O", pykey);

            if (pybool == Py_True) {
                // Strip off the plugin name from list item and add to list.
                pystr = PyUnicode_Replace(pystr, pykey, pyemp, 1); // NR.
                PyList_Append(pyret, pystr);
                Py_DECREF(pystr);
            }
            Py_DECREF(pybool);
        }
        Py_DECREF(pylst);
        Py_DECREF(pyemp);
        Py_DECREF(pyplugin);
        Py_DECREF(pykey);
    }
    else {
        pyret = Py_None;
        Py_INCREF(pyret); 
    }
    return pyret;
}

/**
 * Capsule destructor for attrs pointers allocated with 
 * hexchat_event_attrs_create().
 */
void py_attrs_free_fn(PyObject *pyattrs)
{
    hexchat_event_attrs *attrs = PyCapsule_GetPointer(pyattrs, "attrs");
    hexchat_event_attrs_free(ph, attrs);
}

/**
 * Free function for hexchat lists. Gets invoked when its related capsule's ref 
 * count falls to 0.
 */
 /*
void py_list_free_fn(PyObject *pyxlist)
{
    hexchat_list *xlist = PyCapsule_GetPointer(pyxlist, "xlist");
    hexchat_list_free(ph, xlist);
}
*/

/**
 * Hook free function. Gets invoked when a hook capsule's ref count goes to 0.
 */
void py_hook_free_fn(PyObject *pyhook) {
    CallbackData *data;
    hexchat_hook *hook;

    hook = PyCapsule_GetPointer(pyhook, "hook");
    if (!hook) {
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
    }
    data = (CallbackData *)PyCapsule_GetContext(pyhook);

    // If hook == NULL, the hook has already been unhooked.
    if (data->hook) {
        hexchat_unhook(ph, hook);
        data->hook = NULL;

        Py_DECREF(data->callback);
        Py_DECREF(data->userdata);
    }
    PyMem_RawFree(data);
}


/**
 * The handler for all various types of callback. When a HexChat event
 * occurs, a callback is invoked, which invokes this function that converts
 * the arguments to Python data, then invokes the Python callback registered 
 * for the event.
 */
int
hc_all_callback_inner(CB_VER ver, char *word[], char *word_eol[], 
                      hexchat_event_attrs *attrs, void *userdata)
{
    PyObject        *pyword     = NULL;
    PyObject        *pyword_eol = NULL;
    PyObject        *pyattrs;
    PyObject        *pystr;
    PyObject        *pybytes;
    PyObject        *pysep;
    PyObject        *pyslice;
    Py_ssize_t      py_len;
    PyObject        *pyret;
    int             retval      = 0;
    CallbackData    *data;
    SwitchTSInfo    tsinfo;

    data = (CallbackData *)userdata;
    
    if (!data->hook) {
        // If the hook is NULL, then it's already been unhooked and this 
        // callback invokation should be ignored.
        return HEXCHAT_EAT_NONE;
    }

    // Switch to the callback owner's sub-interpreter threadstate.
    tsinfo = switch_threadstate(data->threadstate);
    
    // Convert the word[] array to a Python list.
    if (ver & (CBV_PRNT | CBV_PRNT_ATTR | CBV_CMD | CBV_SRV | CBV_SRV_ATTR)) {
        pyword = PyList_New(0);
        for (int i = 1; word[i] && strcmp("", word[i]); i++) {
            // Need to grab text as a bytes object first so it can be decoded
            // replacing bad values with '\U0000fffd'.
            pybytes = PyBytes_FromString(word[i]);
            pystr   = PyUnicode_FromEncodedObject(pybytes, "utf-8", "replace");
            PyList_Append(pyword, pystr);
            Py_DECREF(pystr);
            Py_DECREF(pybytes);
        }
    }
    // Convert the word_eol[] array (if it exists) to a Python list.
    if (ver & (CBV_CMD | CBV_SRV | CBV_SRV_ATTR)) {
        pyword_eol = PyList_New(0);
        for (int i = 1; word_eol[i] && strcmp("", word_eol[i]); i++) {
            pybytes = PyBytes_FromString(word_eol[i]);
            pystr   = PyUnicode_FromEncodedObject(pybytes, "utf-8", "replace");
            PyList_Append(pyword_eol, pystr);
            Py_DECREF(pystr);
            Py_DECREF(pybytes);
        }
    }
    // Construct a word_eol Python list from the word[] array if necessary.
    else if (ver & (CBV_PRNT | CBV_PRNT_ATTR)) {
        py_len      = PyList_Size(pyword);
        pyword_eol  = PyList_New(py_len);
        pysep       = PyUnicode_FromString(" ");

        for (Py_ssize_t i = 0; i < py_len; i++) {
            pyslice = PyList_GetSlice(pyword, i, py_len);
            pystr   = PyUnicode_Join(pysep, pyslice);
            
            PyList_SetItem(pyword_eol, i, pystr);
            Py_DECREF(pyslice);
        }
        Py_DECREF(pysep);
    }
    
    // Invoke the callback.
    if (ver & (CBV_PRNT | CBV_CMD | CBV_SRV)) {
        pyret = PyObject_CallFunction(data->callback, "OOO", pyword, 
                                      pyword_eol, data->userdata);
        Py_DECREF(pyword);
        Py_DECREF(pyword_eol);
    }
    else if (ver & (CBV_PRNT_ATTR | CBV_SRV_ATTR)) {
        // Create an EventAttrs object for the Python callback.
        pyattrs = PyObject_CallFunction((PyObject *)EventAttrsTypePtr, "L",
                                        (long long)attrs->server_time_utc);

        pyret = PyObject_CallFunction(data->callback, "OOOO", pyword, 
                                      pyword_eol, pyattrs, data->userdata);
        Py_DECREF(pyattrs);
        Py_DECREF(pyword);
        Py_DECREF(pyword_eol);
    }
    else { // CBV_TIMER
        pyret = PyObject_CallFunction(data->callback, "O", data->userdata);
    }
    
    if (pyret) {
        // Convert return value; and check for, and report, any errors.
        retval = (int)PyLong_AsLong(pyret);

        if (PyErr_Occurred()) {
            PyErr_Clear();
            PyErr_SetString(PyExc_TypeError,
                            "Command callbacks must return an integer value.");
            PyErr_Print();
            retval = (ver & CBV_TIMER) ? 0 : HEXCHAT_EAT_NONE;
        }
        else if (ver != CBV_TIMER && 
                 !(HEXCHAT_EAT_NONE <= retval && retval <= HEXCHAT_EAT_ALL)) {
                     
            PyErr_SetString(PyExc_TypeError,
                            "Non-timer callbacks must return one of these "
                            "values: "
                            "EAT_NONE(0), EAT_HEXCHAT(1), EAT_PLUGIN(2), or "
                            "EAT_ALL(3).");
            PyErr_Print();
            retval = HEXCHAT_EAT_NONE;
        }
        Py_DECREF(pyret);
    }
    else {
        // There was an error in the callback.
        PyErr_Print();
        retval = (ver & CBV_TIMER) ? 0 : HEXCHAT_EAT_NONE;
    }

    // Switch back to the previous threadstate.
    switch_threadstate_back(tsinfo);

    return retval;
}

int
hc_print_callback(char *word[], void *userdata) 
{
    return hc_all_callback_inner(CBV_PRNT, word, NULL, NULL, userdata);
}

int
hc_print_attrs_callback(char *word[], hexchat_event_attrs *attrs, 
                        void *userdata)
{
    return hc_all_callback_inner(CBV_PRNT_ATTR, word, NULL, attrs, userdata);
}

int
hc_command_callback(char *word[], char *word_eol[], void *userdata)
{
    return hc_all_callback_inner(CBV_CMD, word, word_eol, NULL, userdata);
}

int
hc_timer_callback(void *userdata)
{
    return hc_all_callback_inner(CBV_TIMER, NULL, NULL, NULL, userdata);
}

int
hc_server_callback(char *word[], char *word_eol[], void *userdata)
{
    return hc_all_callback_inner(CBV_SRV, word, word_eol, NULL, userdata);
}

int
hc_server_attrs_callback(char *word[], char *word_eol[], 
                         hexchat_event_attrs *attrs, void *userdata)
{
    return hc_all_callback_inner(CBV_SRV_ATTR, word, word_eol, attrs, userdata);
}

static inline int
pystrmatch(PyObject *pystr, char *str)
{
    return PyUnicode_CompareWithASCIIString(pystr, str) == 0;
}

static inline int
len_params(char *word[]) 
{
    int count;
    for (count = 1; word[count] && strcmp(word[count], ""); count++);
    return count - 1;
}

/**
 * Implements the /MPY XXX commands. This is a callback registered for the
 * text event /MPY.
 */
int
mpy_callback(char *word[], char *word_eol[], void *userdata)
{
    PyObject     *pystr, *pycmd;
    SwitchTSInfo tsinfo;
    int          retval;
    int          len_word;
    //int          len_word_eol;

    static const char *help =
        "\00311Usage: /MPY LOAD     <filename>\n"
        "\00311            UNLOAD   <filename | name>\n"
        "\00311            RELOAD   <filename | name>\n"
        "\00311            LIST\n"
        "\00311            EXEC     <command>\n"
        "\00311            CONSOLE\n"
        "\00311            ABOUT";

    tsinfo = switch_threadstate(py_g_main_threadstate);

    pystr = PyUnicode_FromString(word[2]);
    pycmd = PyObject_CallMethod(pystr, "upper", NULL);
    
    len_word     = len_params(word);
    //len_word_eol = len_params(word_eol);

    if      (len_word >= 3 && pystrmatch(pycmd, "LOAD")) {

        retval = load_plugin(word_eol[3]);
    }
    else if (len_word >= 3 && pystrmatch(pycmd, "UNLOAD")) {

        retval = unload_plugin(word_eol[3]);
    }
    else if (len_word >= 3 && pystrmatch(pycmd, "RELOAD")) {

        if (unload_plugin(word_eol[3]) == HEXCHAT_EAT_ALL) {
            retval = load_plugin(word_eol[3]);
        }
        else {
            retval = HEXCHAT_EAT_NONE;
        }
    }
    else if (len_word == 2 && pystrmatch(pycmd, "LIST")) {

        hexchat_printf(ph, "Not implemented yet: %s.", word[2]);
        retval = HEXCHAT_EAT_ALL;
    }
    else if (len_word >= 3 && pystrmatch(pycmd, "EXEC")) {

        exec_console_command(word_eol[3]);
        retval = HEXCHAT_EAT_ALL;
    }
    else if (len_word == 2 && pystrmatch(pycmd, "CONSOLE")) {

        retval = create_console();
    }
    else if (len_word == 2 && pystrmatch(pycmd, "ABOUT")) {

        hexchat_printf(ph, "Not implemented yet: %s.", word[2]);
        retval = HEXCHAT_EAT_ALL;
    }
    else {
        hexchat_print(ph, help);
        retval = HEXCHAT_EAT_ALL;
    }

    Py_DECREF(pystr);
    Py_DECREF(pycmd);

    switch_threadstate_back(tsinfo);

    return retval;
}




