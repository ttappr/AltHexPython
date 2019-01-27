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
 * A list iter closely models how hexchat list pointers work. An internal
 * pointer to a HexChat list is held. next() can be called on a ListIter obj
 * to advance the internal pointer. iter() called on a ListIter object returns
 * the ListIter object itself. This makes it possible to iterate quickly over
 * lists using loop syntax and accessing the fields the internal pointer is
 * currently set to.
 */

#include "minpython.h"

/**
 * ListIter instance data.
 */
typedef struct {
    PyObject_HEAD
    PyObject        *xlist_name;
    PyObject        *field_names;
    PyObject        *type_dict;
    hexchat_list    *xlist_ptr;
    int             nitem;
} ListIterObj;

static int      ListIter_init           (ListIterObj *, PyObject *, PyObject *);
static void     ListIter_dealloc        (ListIterObj *);
static PyObject *ListIter_next          (ListIterObj *);
static PyObject *ListIter_iter          (ListIterObj *);
static PyObject *ListIter_getattro      (ListIterObj *, PyObject *);
static PyObject *ListIter_dir           (ListIterObj *);

static PyObject *ListIter_get_list_name     (ListIterObj *, void *);
static PyObject *ListIter_get_field_names   (ListIterObj *, void *);

static PyObject *get_lists_info             (ListIterObj *);
static void     listiter_create_lists_info_dict(void);

/**
 * ListIter methods.
 */
static PyMethodDef ListIter_methods[] = {
    {"__dir__",   (PyCFunction)ListIter_dir,      METH_NOARGS,
     "Returns attributes of ListIter, which include the field names of the "
     "list."},

    {NULL}
};

/**
 * ListIter accessors.
 */
static PyGetSetDef ListIter_accessors[] = {
    {"list_name",   (getter)ListIter_get_list_name,     (setter)NULL,
     "The list name for the iterator.", NULL },
     
    {"field_names", (getter)ListIter_get_field_names,   (setter)NULL,
     "The field names for the list items.", NULL },
     
    {NULL}
};

/**
 * ListIter type declaration/instance.
 */
static PyTypeObject ListIterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name        = "hexchat.ListIter",
    .tp_doc         = "HexChat list iterator.",
    .tp_basicsize   = sizeof(ListIterObj),
    .tp_itemsize    = 0,
    .tp_flags       = Py_TPFLAGS_DEFAULT,
    .tp_new         = PyType_GenericNew,
    .tp_init        = (initproc)ListIter_init,
    .tp_dealloc     = (destructor)ListIter_dealloc,
    //.tp_members     = ListIter_members,
    .tp_methods     = ListIter_methods,
    .tp_getset      = ListIter_accessors,
    .tp_iter        = (getiterfunc)ListIter_iter,
    .tp_iternext    = (iternextfunc)ListIter_next,
    .tp_getattro    = (getattrofunc)ListIter_getattro,    
};

/**
 * ListIter convenience ptr.
 */
PyTypeObject *ListIterTypePtr = &ListIterType;

/**
 * Constructor.
 * @param self      - instance.
 * @param args      - From Python takes a string for the name of the list to
 *                    grab an internal hexchat_list pointer to.
 * @returns - 0 on success, -1 on failure with error state set.
 */
static int
ListIter_init(ListIterObj *self, PyObject *args, PyObject *Py_UNUSED(kwargs))
{
    PyObject        *pyname;
    const char      *name;
    PyObject        *pylists_info;
    PyObject        *pyfield_info;

    self->xlist_ptr  = NULL;
    self->xlist_name = self->field_names, self->type_dict = NULL;

    if (!PyArg_ParseTuple(args, "U:init", &pyname)) {
        self->xlist_ptr = NULL;
        return -1;
    }
    name = PyUnicode_AsUTF8(pyname);

    // Get a list pointer.
    self->xlist_ptr = hexchat_list_get(ph, name);

    if (!self->xlist_ptr) {
        PyErr_Format(PyExc_RuntimeError, "Bad list type requested (%s).", 
                     name);
        return -1;
    }

    // Get the field names and types.
    if (!(pylists_info = get_lists_info(self))) {
        return -1;
    }
    pyfield_info = PyDict_GetItem(pylists_info, pyname);  // BR.

    if (!pyfield_info) {
        PyErr_Format(PyExc_RuntimeError, 
                     "No type information for list (%s).",
                     PyUnicode_AsUTF8(pyname));
        return -1;
    }
    
    self->xlist_name  = pyname;
    self->field_names = PyTuple_GetItem(pyfield_info, 0);
    self->type_dict   = PyTuple_GetItem(pyfield_info, 1);
    
    Py_INCREF(self->xlist_name);
    Py_INCREF(self->field_names);
    Py_INCREF(self->type_dict);

    Py_DECREF(pylists_info);

    return 0;
}

/**
 * Destructor.
 */
static void
ListIter_dealloc(ListIterObj *self)
{
    if (self->xlist_ptr) {
        hexchat_list_free(ph, self->xlist_ptr);
    }
    Py_XDECREF(self->xlist_name);
    Py_XDECREF(self->field_names);
    Py_XDECREF(self->type_dict);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/**
 * Implements ListIter.__next__(). This gets invoked when next() is invoked on
 * a ListIter object.
 * @returns - self with refcount incremented. Throws PyExc_StopIteration if
 *            at the end of the list.
 */
PyObject *
ListIter_next(ListIterObj *self)
{
    if (hexchat_list_next(ph, self->xlist_ptr) == 0) {
        PyErr_SetString(PyExc_StopIteration, "End of list.");
        return NULL;
    }
    self->nitem++;
    Py_INCREF(self);
    return (PyObject *)self;
}

PyObject *
ListIter_iter(ListIterObj *self)
{
    Py_INCREF(self);
    return (PyObject *)self;
}

/** 
 * Looks up attributes of ListIter objects and returns them. This is similar 
 * to the __getattr__() method of Python classes.
 * @param pyname - The name of the attribute to look up.
 * @returns The attribute requested.
 */
PyObject *
ListIter_getattro(ListIterObj *self, PyObject *pyname)
{
    PyObject        *pyattr_type;
    char            attr_type;
    PyObject        *pyretval;
    int             ival;
    const char      *sval;
    PyObject        *pybytes;
    time_t          tval;
    void            *pval;
    hexchat_list    *xlist;
    const char      *attr_name;
    const char      *list_name;
    PyObject        *pycap;
    
    // See if the requested attribute is defined elsewhere first.
    if ((pyretval = PyObject_GenericGetAttr((PyObject *)self, pyname))) {
        return pyretval;
    }
    // An AttributeError is expected if the attribute isn't one of the generics.

    // If the error is an AttributeError, dismiss it; otherwise, return NULL to 
    // propagate the error.
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) {
        return NULL;
    }
    PyErr_Clear();

    // Get the list name and the attribute name and its type. Use these to 
    // retrieve the value.
    
    attr_name   = PyUnicode_AsUTF8(pyname);
    list_name   = PyUnicode_AsUTF8(self->xlist_name);
    pyattr_type = PyDict_GetItem(self->type_dict, pyname); // Borrowed ref.

    if (!pyattr_type) {
        PyErr_Format(PyExc_AttributeError, 
                     "Unknown field (<%s-list-item>.%s).", 
                     list_name, attr_name);
        return NULL;
    }

    if (self->nitem == 0) {
        PyErr_SetString(PyExc_RuntimeError,
            "next(<list-iterator>) must be invoked before accessing item "
            "attributes.");
        return NULL;
    }

    attr_type   = (char)PyLong_AsLong(pyattr_type);
    xlist       = self->xlist_ptr;
    
    switch (attr_type)
    {
        case 's' : // string. 
            sval = hexchat_list_str(ph, xlist, attr_name);
            if (sval) {
                // Occasionally, I've seen UTF-8 data come through this route
                // with unknown characters. This is why the native string is 
                // put in a bytes object first, then decoded with "replace". 
                // Otherwise, an exception could be thrown which wouldn't be
                // the fault of the plugins. Undecodable data will be replaced 
                // with '\U0000fffd' (a badge with a question mark in it).
                pybytes  = PyBytes_FromString(sval);
                pyretval = PyUnicode_FromEncodedObject(pybytes, "utf-8", 
                                                                "replace");
                Py_DECREF(pybytes);
            }
            else {
                pyretval = PyUnicode_FromString("");
            }
            break;
        case 'i' : // int. 
            ival     = hexchat_list_int(ph, xlist, attr_name);
            pyretval = PyLong_FromLong(ival);
            break;
        case 'p' : // pointer.
            if (!strcmp("channels", list_name) && 
                !strcmp("context", attr_name)) {
                // Currently channels.context is the only list field that 
                // is a void pointer type.
                pval = (void *)hexchat_list_str(ph, xlist, attr_name);
                if (pval) {
                    pycap = PyCapsule_New(pval, "context", NULL);
                    pyretval = PyObject_CallFunction(
                                    (PyObject *)ContextTypePtr,
                                    "OOO", Py_None, Py_None, pycap);
                    Py_DECREF(pycap);
                }
                else {
                    pyretval = Py_None;
                    Py_INCREF(pyretval);
                }
            }
            else {
                PyErr_Format(PyExc_RuntimeError, 
                             "Pointer type requested for <%s-list-item>.%s "
                             "unsupported.",
                             list_name, attr_name);
                return NULL;
            }
            break;
        case 't' : // time.
            tval     = hexchat_list_time(ph, xlist, attr_name);
            pyretval = PyLong_FromLongLong((long long)tval);
            break;
        default : // unknown.
            PyErr_Format(PyExc_RuntimeError, 
                         "Unsupported field type(%c) for <%s-list-item>.%s", 
                         attr_type, list_name, attr_name);
            return NULL;
    }
    return pyretval;
}

/**
 * Accessor that returns the name of the hexchat_list.
 */
PyObject *ListIter_get_list_name(ListIterObj *self, void *args)
{
    Py_INCREF(self->xlist_name);
    return self->xlist_name;
}

/**
 * Getter for the names of the fields of the list. The fields of the list
 * item currently pointed to by the internal pointer are accessible through
 * the ListIter object.
 */
PyObject *ListIter_get_field_names(ListIterObj *self, void *args)
{
    Py_INCREF(self->field_names);
    return self->field_names;
}

/**
 * Returns a list of the class/object attributes. This is called when dir(o)
 * is invoked on the ListIter class or an object.
 */
PyObject *ListIter_dir(ListIterObj *self)
{
    PyObject    *pydir_list1;
    PyObject    *pydir_list2;

    pydir_list1 = PyObject_Dir((PyObject *)Py_TYPE(self));
    pydir_list2 = PySequence_InPlaceConcat(pydir_list1, self->field_names);

    Py_DECREF(pydir_list1);

    if (!pydir_list2) {
        // This should not happen.
        PyErr_SetString(PyExc_RuntimeError, 
                        "Failed to concatenate field names to dir(o) list.");
    }

    return pydir_list2;
}

/**
* Creates the dictionary used to look up the field names and data types of the
* fields for hexchat_list items.
*
* The dictionary is of the form:
*
*     { "<list-name>" : (  (<field-name>, ... ),
*                          { "<field-name>" : <type>, ... }  ), ... }
*/
void
listiter_create_lists_info_dict()
{
    PyObject            *pytype_dict;
    PyObject            *pyfield_dict;
    PyObject            *pyfield_names;
    PyObject            *pylist_info;
    int                 llen;
    PyObject            *pyfname;
    PyObject            *pyftype;
    const char *const   *list_fields;
    const char          *list_types[] = { "channels", "dcc", "ignore", "notify",
                                          "users", NULL };
    pytype_dict = interp_get_lists_info(); // BR.

    // Iterate over the list names, populating a dictionary keyed on list name
    // with tuple values. The tuples have a list of field names and a dictionary
    // of field types (keyed on field name).
    for (int i = 0; list_types[i] != NULL; i++) {

        list_fields = hexchat_list_fields(ph, list_types[i]);
        pyfield_dict = PyDict_New();

        // Populate the field-type-dict - keyed on field name with data type 
        // values.
        llen = 0;
        for (int j = 0; list_fields[j] != NULL; j++, llen++) {

            // The field name starts after the first character.
            pyfname = PyUnicode_FromString(&list_fields[j][1]);

            // The first character of the field name indicates the data type.
            pyftype = PyLong_FromUnsignedLong((unsigned long)
                                              list_fields[j][0]);
            PyDict_SetItem(pyfield_dict, pyfname, pyftype);
            Py_DECREF(pyfname);
            Py_DECREF(pyftype);
        }

        // Construct the list of field names.
        pyfield_names = PyTuple_New(llen);

        for (int j = 0; j < llen; j++) {
            PyTuple_SetItem(pyfield_names, j,
                            PyUnicode_FromString(&list_fields[j][1]));
        }
        pylist_info = PyTuple_New(2);
        PyTuple_SetItem(pylist_info, 0, pyfield_names);
        PyTuple_SetItem(pylist_info, 1, pyfield_dict);

        // Build the type dictionary. Format of the structure:
        //    <list-name> : (<field-names>, <field-type-dict>)
        PyDict_SetItemString(pytype_dict,
                             list_types[i],
                             pylist_info);
        Py_DECREF(pylist_info);
    }
}

/**
* Used internally - On first access, a nested dictionary is constructed.
*/
PyObject *
get_lists_info(ListIterObj *self)
{
    PyObject *pylists_info = interp_get_lists_info();

    if (PyObject_Length(pylists_info) == 0) {
        listiter_create_lists_info_dict();
    }

    Py_INCREF(pylists_info);
    return pylists_info;
}

















