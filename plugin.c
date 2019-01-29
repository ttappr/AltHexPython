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
 * This file holds functions and structures specifically relevant to the
 * mangement of plugins written in Python.
 */

#include "minpython.h"

#define MAX_IDCHR    512 

/**
 * Plugin info. Linked list item.
 */
typedef struct _PluginData {
    struct
    _PluginData     *next;
    PyThreadState   *threadstate;
    void            *plugin_handle;
    PyObject        *name;
    PyObject        *path;
} PluginData;

/**
 * Linked list root for plugin info.
 */
static PluginData plugin_data = { .next = NULL };

int                 init_plugins            (void);
int                 delete_plugins          (void);
int                 load_plugin             (char *);
int                 unload_plugin           (char *);

static int          load_plugin_callback    (char *[], char *[], void *);
static int          unload_plugin_callback  (char *[], char *[], void *);
static int          reload_plugin_callback  (char *[], char *[], void *);

static void         plugin_list_add         (PyObject *, PyObject *, 
                                             PyThreadState *, void *);
//static PluginData   *plugin_list_find_ts    (PyThreadState *);
static PluginData   *plugin_list_remove     (PyObject *);
static void         plugin_list_clear       (void);


/**
 * Initializes the plugins module. This is called when MinPython is loaded.
 * Callbacks are registered for LOAD, UNLOAD, and RELOAD.
 * @returns - 0.
 */
int 
init_plugins()
{
    hexchat_hook_command(ph, "LOAD",    HEXCHAT_PRI_NORM, 
                         load_plugin_callback,
                         "Handles load events for MinPython plugins.", NULL);
                         
    hexchat_hook_command(ph, "UNLOAD",  HEXCHAT_PRI_NORM, 
                         unload_plugin_callback,
                         "Handles unload events for MinPython plugins.", NULL);
                         
    hexchat_hook_command(ph, "RELOAD",  HEXCHAT_PRI_NORM, 
                         reload_plugin_callback,
                         "Handles reload events for MinPython plugins.", NULL);    
    return 0;
}

/**
 * De-initializes the plugins module. All loaded plugins are removed. This is
 * called when the MinPython plugin itself is unloaded.
 * @returns - 0.
 */
int
delete_plugins()
{
    plugin_list_clear();
    return 0;
}

/**
 * Passed to create_interp() to do additional configuration of new interp.
 * @param ts        - The threadstate of the plugin's interp.
 * @param userdata  - The userdata passed to create_interp() with this function.
 * @returns - 0 on success, -1 on failure with error state set.
 */
static int 
create_interp_callback(PyThreadState *ts, void *userdata) 
{
    PyObject   *pymain_module;
    PyObject   *pymain_dict;
    PyObject   *pystr;
    PyObject   *pymodname, *pypath;
    const char *modname;
    const char *version;
    const char *desc;
    const char *path;
    void       *plugin_handle;
    FILE       *fp;
    int        script_ret;
    
    fp   = (FILE *)((void **)userdata)[0];
    path = (char *)((void **)userdata)[1];
    
    // Run the Python script.
    script_ret = PyRun_SimpleFile(fp, path);
    
    fclose(fp);    
    
    if (!script_ret) {
        // Good so far - retrieve needed information from the sub-interpter.
        pymain_module = PyImport_AddModule("__main__"); // BR
        pymain_dict = PyModule_GetDict(pymain_module);  // BR
        
        if (pymain_dict) {
            pymodname = PyDict_GetItemString(pymain_dict, 
                                             "__module_name__"); // BR
            modname   = (pymodname) ? PyUnicode_AsUTF8(pymodname) : NULL;

            pystr     = PyDict_GetItemString(pymain_dict, 
                                             "__module_version__");
            version   = (pystr) ? PyUnicode_AsUTF8(pystr) : NULL;

            pystr     = PyDict_GetItemString(pymain_dict, 
                                             "__module_description__");
            desc      = (pystr) ? PyUnicode_AsUTF8(pystr) : NULL;

            if (!modname || !version || !desc) {
                hexchat_print(ph, "\00304"
                              "The plugin must set these three variables: "
                              "__module_name__, "
                              "__module_version__, "
                              "and __module_description__.");
                script_ret = -1;
            }
            else {
                // Add the plugin to the HexChat GUI.
                plugin_handle = hexchat_plugingui_add(ph, path, modname,
                    desc, version, NULL);

                pypath = PyUnicode_FromString(path);

                // Add local list item for it.
                plugin_list_add(pymodname, pypath, ts, plugin_handle);

                Py_DECREF(pypath);

                hexchat_printf(ph, "%s loaded.", modname);
            }
        }
    }
    return script_ret;
}

/**
 * Loads the plugin at the given path.
 *
 * @param name_or_path - the file path of the plugin to load. Handles UTF-8 
 *                       encoded.
 * @returns - HEXCHAT_EAT_ALL.
 */
int 
load_plugin(char *name_or_path)
{   
    PyObject   *pyos_module;
    PyObject   *pypath_obj;
    PyObject   *pylibdir;
    PyObject   *pyplugin_path;
    PyObject   *pyenc_path;
    const char *libdir;
    const char *modname;
    FILE       *fp;
    void       *userdata[2]; // fp, path

    // This function will be executing within the main Python interpreter when
    // called.
    
    // This should succeed if path is absolute. However it could just be the
    // name of the file.
    fp = fopen(name_or_path, "r");

    if (!fp) {
        // If fopen() failed, prepend the addons directory to the path
        // and try again.
    
        modname = name_or_path;
    
        // Retrieve the os.path object.
        pyos_module = PyImport_AddModule("os"); // BR
        if (!pyos_module) { 
            PyErr_Print(); 
            return HEXCHAT_EAT_ALL;
        }
        pypath_obj = PyObject_GetAttrString(pyos_module, "path"); // NR
        
        // Get the addons dir path.
        libdir   = hexchat_get_info(ph, "xchatdir");
        pylibdir = PyUnicode_DecodeFSDefault(libdir);
        
        if (!pylibdir) {
            Py_DECREF(pypath_obj);
            PyErr_Print();
            return HEXCHAT_EAT_ALL;
        }
        
        // Join the addons path with the file name.
        pyplugin_path = PyObject_CallMethod(pypath_obj, "join", "Oss", 
                                            pylibdir,"addons", modname);
        Py_DECREF(pypath_obj);
        Py_DECREF(pylibdir);

        if (!pyplugin_path) {
            PyErr_Print();
            return HEXCHAT_EAT_ALL;
        }
        pyenc_path = PyUnicode_EncodeFSDefault(pyplugin_path);

        Py_DECREF(pyplugin_path);
        
        if (!pyenc_path) {
            PyErr_Print();
            return HEXCHAT_EAT_ALL;
        }
        
        // Try opening the file again with the full path.
        fp = fopen(PyBytes_AsString(pyenc_path), "r");
        
        Py_DECREF(pyenc_path);
        
        if (!fp) {
            hexchat_printf(ph, "Couldn't load %s.", name_or_path);
            return HEXCHAT_EAT_ALL;
        }
    }    
    userdata[0] = fp;
    userdata[1] = name_or_path;
    
    create_interp(create_interp_callback, &userdata);
    
    return HEXCHAT_EAT_ALL;
}

/**
 * Unloads the Python plugin with the given name or path and removes it from
 * the list. Any hooks it had registered are unhooked.
 *
 * @param name_or_path - the name or path of the plugin to unload. Handles 
 *                       UTF-8 encoded
 * @returns - HEXCHAT_EAT_ALL on success, HEXCHAT_EAT_NONE if not found.
 */
int
unload_plugin(char *name_or_path)
{
    PluginData  *pd;
    PyObject    *pyname;

    pyname = PyUnicode_FromString(name_or_path);
    
    pd = plugin_list_remove(pyname);
    
    Py_DECREF(pyname);
    
    if (pd) { 
        delete_interp(pd->threadstate, NULL, NULL);
        
        hexchat_plugingui_remove(ph, pd->plugin_handle);
        hexchat_printf(ph, "%s unloaded.", PyUnicode_AsUTF8(pd->name));

        Py_DECREF(pd->name);
        Py_DECREF(pd->path);
        
        PyMem_RawFree(pd);

        return HEXCHAT_EAT_ALL;
    }
    return HEXCHAT_EAT_NONE;
}

/**
 * /LOAD command callback for Python plugins.
 */
int
load_plugin_callback(char *word[], char *word_eol[], void *userdata) 
{
    PyObject        *pypath;
    PyObject        *pystr;
    PyObject        *pycmp;
    SwitchTSInfo    tsinfo;
    int             retval;

    tsinfo = switch_threadstate(py_g_main_threadstate);

    pypath = PyUnicode_DecodeFSDefault(word_eol[2]);
    pystr  = PyObject_CallMethod(pypath, "lower", NULL);
    pycmp  = PyObject_CallMethod(pystr, "endswith", "s", ".py");

    retval = HEXCHAT_EAT_NONE;

    if (pycmp == Py_True) {
        load_plugin(word_eol[2]);
        retval = HEXCHAT_EAT_ALL;
    }

    Py_DECREF(pypath);
    Py_DECREF(pystr);
    Py_DECREF(pycmp);

    switch_threadstate_back(tsinfo);

    return retval;
}

/**
 * /UNLOAD callback for Python plugins.
 */
int
unload_plugin_callback(char *word[], char *word_eol[], void *userdata)
{
    return unload_plugin(word_eol[2]);
}

/**
 * /RELOAD callback for Python plugins.
 */
int
reload_plugin_callback(char *word[], char *word_eol[], void *userdata)
{
    int retval = HEXCHAT_EAT_NONE; 
    
    if (unload_plugin(word_eol[2]) == HEXCHAT_EAT_ALL) {
        retval = load_plugin(word_eol[2]);
    }
    return retval;
}

/**
 * Adds a plugin list item to the linked list.
 * @param name      - The name of the plugin.
 * @param path      - The path to the plugin Python file.
 * @param ts        - The threadstate of the plugin's interpreter.
 * @param plugin_handle - The handle returned by hexchat_plugingui_add() when
 *                        it was registered with hexchat.
 */
void 
plugin_list_add(PyObject *name, PyObject *path, 
                PyThreadState *ts, void *plugin_handle)
{
    PluginData *pd = &plugin_data;

    for (; pd->next; pd = pd->next);

    pd->next            = PyMem_RawMalloc(sizeof(PluginData));
    pd                  = pd->next;
    pd->threadstate     = ts;
    pd->plugin_handle   = plugin_handle;
    pd->next            = NULL;

    pd->name            = name;
    pd->path            = path;
    
    Py_INCREF(name);
    Py_INCREF(path);
}

/**
 * Finds the plugin data with the given threadstate. Returns its pointer value
 * if found, or NULL if not found.
 */
 /*
PluginData *
plugin_list_find_ts(PyThreadState *ts)
{
    PluginData    *pd;

    for (pd = plugin_data.next; pd && pd->threadstate != ts; pd = pd->next);
    return pd;
}
*/

/**
 * Removes the named item from the list and returns it, or NULL if not found.
 *
 * @param name_or_path - the name or path of the plugin to remove.
 * @returns - the PluginData for the plugin, or NULL if not found. The data
 *            object needs to be freed by the caller.
 */
PluginData *
plugin_list_remove(PyObject *name_or_path)
{
    PluginData    *pd      = &plugin_data;
    PluginData    *found   = NULL;

    for (; pd->next; pd = pd->next) {
        
        if (!PyUnicode_Compare(pd->next->name, name_or_path) ||
            !PyUnicode_Compare(pd->next->path, name_or_path)) {

            found = pd->next;
            break;
        }
    }
    if (found) {
        pd->next = pd->next->next;
    }
    return found;
}

/**
 * Unloads all Python plugins, clearing the linked list, and freeing memory.
 */
void
plugin_list_clear()
{
    PluginData *pd = plugin_data.next;
    
    while (pd) {
        unload_plugin(PyUnicode_AsUTF8(pd->path));
        pd = plugin_data.next;
    }
}

