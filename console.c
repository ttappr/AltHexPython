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
 * The Console is a subinterpreter that exists for the duration of the
 * Python plugin, from load to unload or HexChat exit. Invoking the text
 * command /MPY EXEC passes text to it for execution from any window. There is
 * an interactive Console window that can be brought up via /MPY CONSOLE.
 * The same subinterpreter exists across all windows, including the Console.
 */

#include "minpython.h"

/**
 * Data for the console and single global instance of the console interpreter.
 */
typedef struct {
    PyThreadState   *threadstate;
    hexchat_context *console_ctx;
    hexchat_hook    *your_msg_hook;
    hexchat_hook    *srvr_msg_hook;
    hexchat_hook    *closectx_hook;
    PyObject        *globals;
    PyObject        *locals;
} ConsoleData;

/**
 * The Console instance.
 */
static ConsoleData console_interp_data = { NULL, NULL, NULL, NULL, 
                                           NULL, NULL, NULL };

static int python_command_callback  (char *[], void *);
static int server_text_callback     (char *[], void *);
static int close_context_callback   (char *[], void *);
int        create_console_interp    (void);
int        delete_console_interp    (void);
int        exec_console_command     (const char *);
int        create_console           (void);
int        close_console            (void);

/**
 * Callback passed to create_interp(). Sets the module name and sets up stdout
 * for Python code colorization.
 * @param ts        - The Console interp's main threadstate.
 * @param userdata  - Userdata given to create_interp().
 * @returns 0 (success)
 */
static int
create_callback(PyThreadState *ts, void *userdata)
{
    PyObject    *pymodule;
    PyObject    *pyglobals;
    PyObject    *pystdout;
    PyObject    *pymodname;
    ConsoleData *data = &console_interp_data;
    
    pymodule  = PyImport_AddModule("__main__");  // Borrowd ref.
    pyglobals = PyModule_GetDict(pymodule);      // Borrowed ref.

    Py_INCREF(pyglobals);

    // Set up the callback data.
    data->threadstate = ts;
    data->globals     = pyglobals;
    data->locals      = pyglobals;
    
    pymodname = PyUnicode_FromString("Console");
    PyDict_SetItemString(pyglobals, "__module_name__", pymodname);
    Py_DECREF(pymodname);

    // Turn on code colorization.
    pystdout = PySys_GetObject("stdout"); // Borrowed ref.
    PyObject_SetAttrString(pystdout, "colorize_on", Py_True);

    return 0;
}

/**
 * Creates the console interpreter. Called when the Python plugin is loaded.
 * @returns 0 (success).
 */
int
create_console_interp()
{
    ConsoleData *data = &console_interp_data;

    if (!data->threadstate) {
        create_interp(create_callback, NULL);
    }
    return 0;
}

/**
 * Callback passed to delete_interp().
 * @returns 0 (success).
 */
static int 
delete_callback(PyThreadState *ts, void *userdata)
{
    ConsoleData *data = (ConsoleData *)userdata;
    
    Py_DECREF(data->globals);

    data->globals       = NULL;
    data->locals        = NULL;
    data->threadstate   = NULL;
    
    return 0;
}

/**
 * Deletes the console interpreter. Called when the plugin is unloaded, or 
 * during shutdown.
 */
int
delete_console_interp()
{
    ConsoleData *data = &console_interp_data;

    delete_interp(data->threadstate, delete_callback, data);

    return 0;
}

/**
 * Executes the provided script string. This is called in the console and with
 * the /MPY EXEC command.
 * @param script    - The code to execute in the Console interp.
 * @returns         - 0 on success, -1 on fail.
 */
int
exec_console_command(const char *script)
{
    ConsoleData     *data;
    SwitchTSInfo    tsinfo;
    PyObject        *pyresult, *pystr;
    int             retval;

    data = &console_interp_data;

    // Switch threadstate.
    tsinfo = switch_threadstate(data->threadstate);

    pystr = PyUnicode_FromFormat("%s\n", script);

    // Print command.
    PySys_FormatStdout(">>> %U", pystr);

    // Run command.
    pyresult = PyRun_String(PyUnicode_AsUTF8(pystr), Py_single_input,
                            data->globals, data->locals);
    Py_DECREF(pystr);

    if (!pyresult) {
        PyErr_Print();
        retval = -1;
    }
    else {
        Py_DECREF(pyresult);
        retval = 0;
    }

    // Switch back.
    switch_threadstate_back(tsinfo);

    return retval;
}

/**
 * Opens the console widow.
 * @returns - HEXCHAT_EAT_ALL on success, HEXCHAT_EAT_NONE on failure to create
 *            the interp.
 */
int 
create_console()
{
    hexchat_context *ctx;
    ConsoleData     *data;

    data = &console_interp_data;

    // Grab the console context if it already exists.
    ctx = hexchat_find_context(ph, NULL, ">>minpython<<");

    if (ctx) {
        // The console is already created in the current server context.
        hexchat_set_context(ph, ctx);
        hexchat_command(ph, "GUI FOCUS");
        return HEXCHAT_EAT_ALL;
    }

    // Create the console window.
    hexchat_command(ph, "QUERY >>minpython<<");
    ctx = hexchat_find_context(ph, NULL, ">>minpython<<");

    data->console_ctx = ctx;

    if (create_console_interp()) {
        // Should have already been created.
        return HEXCHAT_EAT_NONE;
    }

    // Register hexchat callbacks.
    data->your_msg_hook = hexchat_hook_print(
                            ph, "Your Message", HEXCHAT_PRI_NORM, 
                            python_command_callback, data);

    data->srvr_msg_hook = hexchat_hook_print(
                            ph, "Server Text", HEXCHAT_PRI_NORM,
                            server_text_callback, data);

    data->closectx_hook = hexchat_hook_print(
                            ph, "Close Context", HEXCHAT_PRI_NORM,
                            close_context_callback, data);
    return HEXCHAT_EAT_ALL;
}

/**
 * Closes the console if it's open. The interp is not destroyed.
 * @returns HEXCHAT_EAT_ALL.
 */
int
close_console()
{
    hexchat_context *ctx;

    // Grab the console context if it exists.
    ctx = hexchat_find_context(ph, NULL, ">>minpython<<");

    if (ctx) {
        // Close it.
        hexchat_set_context(ph, ctx);
        hexchat_command(ph, "CLOSE");
    }
    return HEXCHAT_EAT_ALL;
}

/**
 * Callback invoked when the user enters script text in the console. Passes
 * text to the Console interpreter for execution.
 * @param word      - Word data received from event.
 * @param userdata  - The Console's ConsoleData instance.
 * @returns     - HEXCHAT_EAT_ALL on success, HEXCHAT_EAT_NONE if the
 *                an event is received from another context.
 */
int 
python_command_callback(char *word[], void *userdata)
{
    ConsoleData     *data;
    hexchat_context *ctx;

    data = (ConsoleData *)userdata;

    ctx = hexchat_get_context(ph);

    if (ctx != data->console_ctx) {
        return HEXCHAT_EAT_NONE;
    }

    exec_console_command(word[2]);

    return HEXCHAT_EAT_ALL;
}

/**
 * Suppresses server text messages in the console window.
 * @param word      - Word data received from event.
 * @param userdata  - The Console's ConsoleData instance.
 * @returns     - HEXCHAT_EAT_ALL if event received for the Console context,
 *                HEXCHAT_EAT_NONE if it was from another context.
 */
int
server_text_callback(char *word[], void *userdata) 
{
    ConsoleData     *data;
    hexchat_context *ctx;

    data = (ConsoleData *)userdata;

    ctx = hexchat_get_context(ph);

    if (ctx != data->console_ctx) {
        return HEXCHAT_EAT_NONE;
    }
    return HEXCHAT_EAT_ALL;
}


/**
 * Callback called when the console context is closed.
 * @param word      - Word data from event.
 * @param userdata  - The Console's ConsoleData instance.
 * @returns HEXCHAT_EAT_ALL if the event came from the Console context,
 *          HEXCHAT_EAT_NONE otherwise.
 */
int
close_context_callback(char *word[], void *userdata) 
{
    ConsoleData     *data;
    hexchat_context *ctx;
    
    data = (ConsoleData *)userdata;

    ctx = hexchat_get_context(ph);

    if (ctx != data->console_ctx) {
        return HEXCHAT_EAT_NONE;
    }

    hexchat_unhook(ph, data->srvr_msg_hook);
    hexchat_unhook(ph, data->your_msg_hook);
    hexchat_unhook(ph, data->closectx_hook);

    data->console_ctx   = NULL;
    data->srvr_msg_hook = NULL;
    data->your_msg_hook = NULL;
    data->closectx_hook = NULL;

    return HEXCHAT_EAT_ALL;
}
