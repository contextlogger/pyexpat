#include <Python.h>
#include <symbian_python_ext_util.h>

extern "C" {

#if PY_VERSION_HEX < 0x020000B1
#include <assert.h>
#endif
#include <ctype.h>

#include "compile.h"
#include "frameobject.h"
#ifdef HAVE_EXPAT_H
#include "expat.h"
#ifdef XML_MAJOR_VERSION
#define EXPAT_VERSION (0x10000 * XML_MAJOR_VERSION \
                       + 0x100 * XML_MINOR_VERSION \
                       + XML_MICRO_VERSION)
#else
/* Assume the oldest Expat that used expat.h and did not have version info */
#define EXPAT_VERSION 0x015f00
#endif
#else /* !defined(HAVE_EXPAT_H) */
#include "xmlparse.h"
/* Assume Expat 1.1 unless told otherwise */
#ifndef EXPAT_VERSION
#define EXPAT_VERSION 0x010100
#endif
#endif /* !defined(HAVE_EXPAT_H) */

#ifndef PyGC_HEAD_SIZE
#define PyGC_HEAD_SIZE 0
#define PyObject_GC_Init(x)
#define PyObject_GC_Fini(m)
#define Py_TPFLAGS_GC 0
#endif

#if (PY_MAJOR_VERSION == 1 && PY_MINOR_VERSION > 5) || (PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION < 2)
/* In Python 1.6, 2.0 and  2.1, disabling Unicode was not possible. */
#define Py_USING_UNICODE
#endif

enum HandlerTypes {
    StartElement,
    EndElement,
    ProcessingInstruction,
    CharacterData,
    UnparsedEntityDecl,
    NotationDecl,
    StartNamespaceDecl,
    EndNamespaceDecl,
    Comment,
    StartCdataSection,
    EndCdataSection,
    Default,
    DefaultHandlerExpand,
    NotStandalone,
    ExternalEntityRef,
#if EXPAT_VERSION >= 0x010200
    StartDoctypeDecl,
    EndDoctypeDecl,
#endif
#if EXPAT_VERSION == 0x010200
    ExternalParsedEntityDecl,
    InternalParsedEntityDecl,
#endif
#if EXPAT_VERSION >= 0x015f00
    EntityDecl,
    XmlDecl,
    ElementDecl,
    AttlistDecl,
#endif
    _DummyDecl
};


/* ----------------------------------------------------- */

/* Declarations for objects of type xmlparser */

typedef struct {
    PyObject_HEAD

    XML_Parser itself;
    int returns_unicode;        /* True if Unicode strings are returned;
                                   if false, UTF-8 strings are returned */
    int ordered_attributes;     /* Return attributes as a list. */
    int specified_attributes;   /* Report only specified attributes. */
    int in_callback;            /* Is a callback active? */
    PyObject **handlers;
    PyCodeObject **tb_code;
    PyObject *ErrorObject;
} xmlparseobject;

#define Xmlparsetype  ((PyTypeObject *) SPyGetGlobalString("Xmlparsetype"))

typedef void (*xmlhandlersetter)(XML_Parser self, void *meth);
typedef void* xmlhandler;

struct HandlerInfo {
    const char *name;
    xmlhandlersetter setter;
    xmlhandler handler;
};

extern const struct HandlerInfo handler_info[];

/* Set an integer attribute on the error object; return true on success,
 * false on an exception.
 */
static int
set_error_attr(PyObject *err, char *name, int value)
{
    PyObject *v = PyInt_FromLong(value);

    if (v != NULL && PyObject_SetAttrString(err, name, v) == -1) {
        Py_DECREF(v);
        return 0;
    }
    return 1;
}

/* Build and set an Expat exception, including positioning
 * information.  Always returns NULL.
 */
static PyObject *
set_error(xmlparseobject *self)
{
    PyObject *err;
    char buffer[256];
    XML_Parser parser = self->itself;
    int lineno = XML_GetErrorLineNumber(parser);
    int column = XML_GetErrorColumnNumber(parser);
    enum XML_Error code = XML_GetErrorCode(parser);

    PyOS_snprintf(buffer, sizeof(buffer), "%.200s: line %i, column %i",
            XML_ErrorString(code), lineno, column);
    err = PyObject_CallFunction(self->ErrorObject, "s", buffer);
    if (  err != NULL
          && set_error_attr(err, "code", code)
          && set_error_attr(err, "offset", column)
          && set_error_attr(err, "lineno", lineno)) {
        PyErr_SetObject(self->ErrorObject, err);
    }
    return NULL;
}


#if EXPAT_VERSION == 0x010200
/* Convert an array of attributes and their values into a Python dict */

static PyObject *
conv_atts_using_string(XML_Char **atts)
{
    PyObject *attrs_obj = NULL;
    XML_Char **attrs_p, **attrs_k = NULL;
    int attrs_len;
    PyObject *rv;

    if ((attrs_obj = PyDict_New()) == NULL) 
        goto finally;
    for (attrs_len = 0, attrs_p = atts; 
         *attrs_p;
         attrs_p++, attrs_len++) {
        if (attrs_len % 2) {
            rv = PyString_FromString(*attrs_p);  
            if (!rv) {
                Py_DECREF(attrs_obj);
                attrs_obj = NULL;
                goto finally;
            }
            if (PyDict_SetItemString(attrs_obj,
                                     (char*)*attrs_k, rv) < 0) {
                Py_DECREF(attrs_obj);
                attrs_obj = NULL;
                goto finally;
            }
            Py_DECREF(rv);
        }
        else 
            attrs_k = attrs_p;
    }
 finally:
    return attrs_obj;
}
#endif

#ifdef Py_USING_UNICODE
#if EXPAT_VERSION == 0x010200
static PyObject *
conv_atts_using_unicode(XML_Char **atts)
{
    PyObject *attrs_obj;
    XML_Char **attrs_p, **attrs_k = NULL;
    int attrs_len;

    if ((attrs_obj = PyDict_New()) == NULL) 
        goto finally;
    for (attrs_len = 0, attrs_p = atts; 
         *attrs_p;
         attrs_p++, attrs_len++) {
        if (attrs_len % 2) {
            PyObject *attr_str, *value_str;
            const char *p = (const char *) (*attrs_k);
            attr_str = PyUnicode_DecodeUTF8(p, strlen(p), "strict"); 
            if (!attr_str) {
                Py_DECREF(attrs_obj);
                attrs_obj = NULL;
                goto finally;
            }
            p = (const char *) *attrs_p;
            value_str = PyUnicode_DecodeUTF8(p, strlen(p), "strict");
            if (!value_str) {
                Py_DECREF(attrs_obj);
                Py_DECREF(attr_str);
                attrs_obj = NULL;
                goto finally;
            }
            if (PyDict_SetItem(attrs_obj, attr_str, value_str) < 0) {
                Py_DECREF(attrs_obj);
                Py_DECREF(attr_str);
                Py_DECREF(value_str);
                attrs_obj = NULL;
                goto finally;
            }
            Py_DECREF(attr_str);
            Py_DECREF(value_str);
        }
        else
            attrs_k = attrs_p;
    }
 finally:
    return attrs_obj;
}
#endif

/* Convert a string of XML_Chars into a Unicode string.
   Returns None if str is a null pointer. */

static PyObject *
conv_string_to_unicode(XML_Char *str)
{
    /* XXX currently this code assumes that XML_Char is 8-bit, 
       and hence in UTF-8.  */
    /* UTF-8 from Expat, Unicode desired */
    if (str == NULL) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    return PyUnicode_DecodeUTF8((const char *)str, 
                                strlen((const char *)str), 
                                "strict");
}

static PyObject *
conv_string_len_to_unicode(const XML_Char *str, int len)
{
    /* XXX currently this code assumes that XML_Char is 8-bit, 
       and hence in UTF-8.  */
    /* UTF-8 from Expat, Unicode desired */
    if (str == NULL) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    return PyUnicode_DecodeUTF8((const char *)str, len, "strict");
}
#endif

/* Convert a string of XML_Chars into an 8-bit Python string.
   Returns None if str is a null pointer. */

static PyObject *
conv_string_to_utf8(XML_Char *str)
{
    /* XXX currently this code assumes that XML_Char is 8-bit, 
       and hence in UTF-8.  */
    /* UTF-8 from Expat, UTF-8 desired */
    if (str == NULL) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    return PyString_FromString((const char *)str);
}

static PyObject *
conv_string_len_to_utf8(const XML_Char *str,  int len) 
{
    /* XXX currently this code assumes that XML_Char is 8-bit, 
       and hence in UTF-8.  */
    /* UTF-8 from Expat, UTF-8 desired */
    if (str == NULL) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    return PyString_FromStringAndSize((const char *)str, len);
}

/* Callback routines */

static void clear_handlers(xmlparseobject *self, int initial);

static void
flag_error(xmlparseobject *self)
{
    clear_handlers(self, 0);
}

static PyCodeObject*
getcode(xmlparseobject *xmlparseobject,
	enum HandlerTypes slot, char* func_name, int lineno)
{
    PyObject *code = NULL;
    PyObject *name = NULL;
    PyObject *nulltuple = NULL;
    PyObject *filename = NULL;

    if (xmlparseobject->tb_code[slot] == NULL) {
        code = PyString_FromString("");
        if (code == NULL)
            goto failed;
        name = PyString_FromString(func_name);
        if (name == NULL)
            goto failed;
        nulltuple = PyTuple_New(0);
        if (nulltuple == NULL)
            goto failed;
        filename = PyString_FromString(__FILE__);
        xmlparseobject->tb_code[slot] =
            PyCode_New(0,		/* argcount */
                       0,		/* nlocals */
                       0,		/* stacksize */
                       0,		/* flags */
                       code,		/* code */
                       nulltuple,	/* consts */
                       nulltuple,	/* names */
                       nulltuple,	/* varnames */
#if PYTHON_API_VERSION >= 1010
                       nulltuple,	/* freevars */
                       nulltuple,	/* cellvars */
#endif
                       filename,	/* filename */
                       name,		/* name */
                       lineno,		/* firstlineno */
                       code		/* lnotab */
                       );
        if (xmlparseobject->tb_code[slot] == NULL)
            goto failed;
        Py_DECREF(code);
        Py_DECREF(nulltuple);
        Py_DECREF(filename);
        Py_DECREF(name);
    }
    return xmlparseobject->tb_code[slot];
 failed:
    Py_XDECREF(code);
    Py_XDECREF(name);
    return NULL;
}

static int
trace_frame(PyThreadState *tstate, PyFrameObject *f, int code, PyObject *val)
{
    int result = 0;
    if (!tstate->use_tracing || tstate->tracing)
	return 0;
    if (tstate->c_profilefunc != NULL) {
	tstate->tracing++;
	result = tstate->c_profilefunc(tstate->c_profileobj,
				       f, code , val);
	tstate->use_tracing = ((tstate->c_tracefunc != NULL)
			       || (tstate->c_profilefunc != NULL));
	tstate->tracing--;
	if (result)
	    return result;
    }
    if (tstate->c_tracefunc != NULL) {
	tstate->tracing++;
	result = tstate->c_tracefunc(tstate->c_traceobj,
				     f, code , val);
	tstate->use_tracing = ((tstate->c_tracefunc != NULL)
			       || (tstate->c_profilefunc != NULL));
	tstate->tracing--;
    }	
    return result;
}

static PyObject*
call_with_frame(PyCodeObject *c, PyObject* func, PyObject* args)
{
    PyThreadState *tstate = PyThreadState_GET();
    PyFrameObject *f;
    PyObject *res;

    if (c == NULL)
        return NULL;
    
    f = PyFrame_New(
                    tstate,			/*back*/
                    c,				/*code*/
                    PyEval_GetGlobals(),	/*globals*/
                    NULL			/*locals*/
                    );
    if (f == NULL)
        return NULL;
    tstate->frame = f;
    if (trace_frame(tstate, f, PyTrace_CALL, Py_None)) {
	Py_DECREF(f);
	return NULL;
    }
    res = PyEval_CallObject(func, args);
    if (res == NULL && tstate->curexc_traceback == NULL)
        PyTraceBack_Here(f);
    else {
	if (trace_frame(tstate, f, PyTrace_RETURN, res)) {
	    Py_XDECREF(res);
	    res = NULL;
	}
    }
    tstate->frame = f->f_back;
    Py_DECREF(f);
    return res;
}

#ifndef Py_USING_UNICODE
#define STRING_CONV_FUNC conv_string_to_utf8
#else
/* Python 1.6 and later versions */
#define STRING_CONV_FUNC (self->returns_unicode \
                          ? conv_string_to_unicode : conv_string_to_utf8)
#endif

static void
my_StartElementHandler(void *userData,
                       const XML_Char *name, const XML_Char **atts)
{
    xmlparseobject *self = (xmlparseobject *)userData;

    if (self->handlers[StartElement]
        && self->handlers[StartElement] != Py_None) {
        PyObject *container, *rv, *args;
        int i, max;

        /* Set max to the number of slots filled in atts[]; max/2 is
         * the number of attributes we need to process.
         */
        if (self->specified_attributes) {
            max = XML_GetSpecifiedAttributeCount(self->itself);
        }
        else {
            max = 0;
            while (atts[max] != NULL)
                max += 2;
        }
        /* Build the container. */
        if (self->ordered_attributes)
            container = PyList_New(max);
        else
            container = PyDict_New();
        if (container == NULL) {
            flag_error(self);
            return;
        }
        for (i = 0; i < max; i += 2) {
            PyObject *n = STRING_CONV_FUNC((XML_Char *) atts[i]);
            PyObject *v;
            if (n == NULL) {
                flag_error(self);
                Py_DECREF(container);
                return;
            }
            v = STRING_CONV_FUNC((XML_Char *) atts[i+1]);
            if (v == NULL) {
                flag_error(self);
                Py_DECREF(container);
                Py_DECREF(n);
                return;
            }
            if (self->ordered_attributes) {
                PyList_SET_ITEM(container, i, n);
                PyList_SET_ITEM(container, i+1, v);
            }
            else if (PyDict_SetItem(container, n, v)) {
                flag_error(self);
                Py_DECREF(n);
                Py_DECREF(v);
                return;
            }
            else {
                Py_DECREF(n);
                Py_DECREF(v);
            }
        }
        args = Py_BuildValue("(O&N)", STRING_CONV_FUNC,name, container);
        if (args == NULL) {
            Py_DECREF(container);
            return;
        }
        /* Container is now a borrowed reference; ignore it. */
        self->in_callback = 1;
        rv = call_with_frame(getcode(self, 
				     StartElement, "StartElement", __LINE__),
                             self->handlers[StartElement], args);
        self->in_callback = 0;
        Py_DECREF(args);
        if (rv == NULL) {
            flag_error(self);
            return;
        }
        Py_DECREF(rv);
    }
}

#define RC_HANDLER(RC, NAME, PARAMS, INIT, PARAM_FORMAT, CONVERSION, \
                RETURN, GETUSERDATA) \
static RC \
my_##NAME##Handler PARAMS {\
    xmlparseobject *self = GETUSERDATA ; \
    PyObject *args = NULL; \
    PyObject *rv = NULL; \
    INIT \
\
    if (self->handlers[NAME] \
        && self->handlers[NAME] != Py_None) { \
        args = Py_BuildValue PARAM_FORMAT ;\
        if (!args) { flag_error(self); return RETURN;} \
        self->in_callback = 1; \
        rv = call_with_frame(getcode(self,NAME,#NAME,__LINE__), \
                             self->handlers[NAME], args); \
        self->in_callback = 0; \
        Py_DECREF(args); \
        if (rv == NULL) { \
            flag_error(self); \
            return RETURN; \
        } \
        CONVERSION \
        Py_DECREF(rv); \
    } \
    return RETURN; \
}

#define VOID_HANDLER(NAME, PARAMS, PARAM_FORMAT) \
	RC_HANDLER(void, NAME, PARAMS, ;, PARAM_FORMAT, ;, ;,\
	(xmlparseobject *)userData)

#define INT_HANDLER(NAME, PARAMS, PARAM_FORMAT)\
	RC_HANDLER(int, NAME, PARAMS, int rc=0;, PARAM_FORMAT, \
			rc = PyInt_AsLong(rv);, rc, \
	(xmlparseobject *)userData)

VOID_HANDLER(EndElement, 
             (void *userData, const XML_Char *name), 
             ("(O&)", STRING_CONV_FUNC, name))

VOID_HANDLER(ProcessingInstruction,
             (void *userData, 
              const XML_Char *target, 
              const XML_Char *data),
             ("(O&O&)",STRING_CONV_FUNC,target, STRING_CONV_FUNC,data))

#ifndef Py_USING_UNICODE
VOID_HANDLER(CharacterData, 
             (void *userData, const XML_Char *data, int len), 
             ("(N)", conv_string_len_to_utf8(data,len)))
#else
VOID_HANDLER(CharacterData, 
             (void *userData, const XML_Char *data, int len), 
             ("(N)", (self->returns_unicode 
                      ? conv_string_len_to_unicode(data,len) 
                      : conv_string_len_to_utf8(data,len))))
#endif

VOID_HANDLER(UnparsedEntityDecl,
             (void *userData, 
              const XML_Char *entityName,
              const XML_Char *base,
              const XML_Char *systemId,
              const XML_Char *publicId,
              const XML_Char *notationName),
             ("(O&O&O&O&O&)", 
              STRING_CONV_FUNC,entityName, STRING_CONV_FUNC,base, 
              STRING_CONV_FUNC,systemId, STRING_CONV_FUNC,publicId, 
              STRING_CONV_FUNC,notationName))

#if EXPAT_VERSION >= 0x015f00
#ifndef Py_USING_UNICODE
VOID_HANDLER(EntityDecl,
             (void *userData,
              const XML_Char *entityName,
              int is_parameter_entity,
              const XML_Char *value,
              int value_length,
              const XML_Char *base,
              const XML_Char *systemId,
              const XML_Char *publicId,
              const XML_Char *notationName),
             ("O&iNO&O&O&O&",
              STRING_CONV_FUNC,entityName, is_parameter_entity,
              conv_string_len_to_utf8(value, value_length),
              STRING_CONV_FUNC,base, STRING_CONV_FUNC,systemId,
              STRING_CONV_FUNC,publicId, STRING_CONV_FUNC,notationName))
#else
VOID_HANDLER(EntityDecl,
             (void *userData,
              const XML_Char *entityName,
              int is_parameter_entity,
              const XML_Char *value,
              int value_length,
              const XML_Char *base,
              const XML_Char *systemId,
              const XML_Char *publicId,
              const XML_Char *notationName),
             ("O&iNO&O&O&O&",
              STRING_CONV_FUNC,entityName, is_parameter_entity,
              (self->returns_unicode 
               ? conv_string_len_to_unicode(value, value_length) 
               : conv_string_len_to_utf8(value, value_length)),
              STRING_CONV_FUNC,base, STRING_CONV_FUNC,systemId,
              STRING_CONV_FUNC,publicId, STRING_CONV_FUNC,notationName))
#endif

VOID_HANDLER(XmlDecl,
             (void *userData,
              const XML_Char *version,
              const XML_Char *encoding,
              int standalone),
             ("(O&O&i)",
              STRING_CONV_FUNC,version, STRING_CONV_FUNC,encoding, 
              standalone))

static PyObject *
conv_content_model(XML_Content * const model,
                   PyObject *(*conv_string)(XML_Char *))
{
    PyObject *result = NULL;
    PyObject *children = PyTuple_New(model->numchildren);
    int i;

    if (children != NULL) {
        assert(model->numchildren < INT_MAX);
        for (i = 0; i < (int)model->numchildren; ++i) {
            PyObject *child = conv_content_model(&model->children[i],
                                                 conv_string);
            if (child == NULL) {
                Py_XDECREF(children);
                return NULL;
            }
            PyTuple_SET_ITEM(children, i, child);
        }
        result = Py_BuildValue("(iiO&N)",
                               model->type, model->quant,
                               conv_string,model->name, children);
    }
    return result;
}

static void
my_ElementDeclHandler(void *userData,
                      const XML_Char *name,
                      XML_Content *model)
{
    xmlparseobject *self = (xmlparseobject *)userData;
    PyObject *args = NULL;

    if (self->handlers[ElementDecl] != NULL
        && self->handlers[ElementDecl] != Py_None) {
        PyObject *rv = NULL;
        PyObject *modelobj, *nameobj;

#ifdef Py_USING_UNICODE
        modelobj = conv_content_model(model,
                                      (self->returns_unicode
                                       ? conv_string_to_unicode
                                       : conv_string_to_utf8));
#else
        modelobj = conv_content_model(model, conv_string_to_utf8);
#endif
        if (modelobj == NULL) {
            flag_error(self);
            goto finally;
        }
        nameobj = PyString_FromString(name);
        if (nameobj == NULL) {
            Py_DECREF(modelobj);
            flag_error(self);
            goto finally;
        }
        args = Py_BuildValue("NN", nameobj, modelobj);
        if (args == NULL) {
            Py_DECREF(modelobj);
            flag_error(self);
            goto finally;
        }
        self->in_callback = 1;
        rv = call_with_frame(getcode(self,
				     ElementDecl, "ElementDecl", __LINE__),
                             self->handlers[ElementDecl], args);
        self->in_callback = 0;
        if (rv == NULL) {
            flag_error(self);
            goto finally;
        }
        Py_DECREF(rv);
    }
 finally:
    Py_XDECREF(args);
    /* XML_FreeContentModel() was introduced in Expat 1.95.6. */
#if EXPAT_VERSION >= 0x015f06
    XML_FreeContentModel(self->itself, model);
#else
    free(model);
#endif
    return;
}

VOID_HANDLER(AttlistDecl,
             (void *userData,
              const XML_Char *elname,
              const XML_Char *attname,
              const XML_Char *att_type,
              const XML_Char *dflt,
              int isrequired),
             ("(O&O&O&O&i)",
              STRING_CONV_FUNC,elname, STRING_CONV_FUNC,attname,
              STRING_CONV_FUNC,att_type, STRING_CONV_FUNC,dflt,
              isrequired))
#endif

VOID_HANDLER(NotationDecl, 
		(void *userData,
			const XML_Char *notationName,
			const XML_Char *base,
			const XML_Char *systemId,
			const XML_Char *publicId),
                ("(O&O&O&O&)", 
		 STRING_CONV_FUNC,notationName, STRING_CONV_FUNC,base, 
		 STRING_CONV_FUNC,systemId, STRING_CONV_FUNC,publicId))

VOID_HANDLER(StartNamespaceDecl,
		(void *userData,
		      const XML_Char *prefix,
		      const XML_Char *uri),
                ("(O&O&)", STRING_CONV_FUNC,prefix, STRING_CONV_FUNC,uri))

VOID_HANDLER(EndNamespaceDecl,
		(void *userData,
		    const XML_Char *prefix),
                ("(O&)", STRING_CONV_FUNC,prefix))

VOID_HANDLER(Comment,
               (void *userData, const XML_Char *prefix),
                ("(O&)", STRING_CONV_FUNC,prefix))

VOID_HANDLER(StartCdataSection,
               (void *userData),
		("()"))
		
VOID_HANDLER(EndCdataSection,
               (void *userData),
		("()"))

#ifndef Py_USING_UNICODE
VOID_HANDLER(Default,
	      (void *userData,  const XML_Char *s, int len),
	      ("(N)", conv_string_len_to_utf8(s,len)))

VOID_HANDLER(DefaultHandlerExpand,
	      (void *userData,  const XML_Char *s, int len),
	      ("(N)", conv_string_len_to_utf8(s,len)))
#else
VOID_HANDLER(Default,
	      (void *userData,  const XML_Char *s, int len),
	      ("(N)", (self->returns_unicode 
		       ? conv_string_len_to_unicode(s,len) 
		       : conv_string_len_to_utf8(s,len))))

VOID_HANDLER(DefaultHandlerExpand,
	      (void *userData,  const XML_Char *s, int len),
	      ("(N)", (self->returns_unicode 
		       ? conv_string_len_to_unicode(s,len) 
		       : conv_string_len_to_utf8(s,len))))
#endif

INT_HANDLER(NotStandalone, 
		(void *userData), 
		("()"))

RC_HANDLER(int, ExternalEntityRef,
		(XML_Parser parser,
		    const XML_Char *context,
		    const XML_Char *base,
		    const XML_Char *systemId,
		    const XML_Char *publicId),
		int rc=0;,
                ("(O&O&O&O&)", 
		 STRING_CONV_FUNC,context, STRING_CONV_FUNC,base, 
		 STRING_CONV_FUNC,systemId, STRING_CONV_FUNC,publicId),
		rc = PyInt_AsLong(rv);, rc,
	   (xmlparseobject *) XML_GetUserData(parser))

/* XXX UnknownEncodingHandler */

#if EXPAT_VERSION == 0x010200
VOID_HANDLER(StartDoctypeDecl,
	     (void *userData, const XML_Char *doctypeName),
	     ("(O&OOi)", STRING_CONV_FUNC,doctypeName,
              Py_None, Py_None, -1))
#elif EXPAT_VERSION >= 0x015f00
VOID_HANDLER(StartDoctypeDecl,
             (void *userData, const XML_Char *doctypeName,
              const XML_Char *sysid, const XML_Char *pubid,
              int has_internal_subset),
             ("(O&O&O&i)", STRING_CONV_FUNC,doctypeName,
              STRING_CONV_FUNC,sysid, STRING_CONV_FUNC,pubid,
              has_internal_subset))
#endif

#if EXPAT_VERSION >= 0x010200
VOID_HANDLER(EndDoctypeDecl, (void *userData), ("()"))
#endif

#if EXPAT_VERSION == 0x010200
VOID_HANDLER(ExternalParsedEntityDecl,
	     (void *userData, const XML_Char *entityName,
	      const XML_Char *base, const XML_Char *systemId,
	      const XML_Char *publicId),
	     ("(O&O&O&O&)", STRING_CONV_FUNC, entityName,
	      STRING_CONV_FUNC, base, STRING_CONV_FUNC, systemId,
	      STRING_CONV_FUNC, publicId))

VOID_HANDLER(InternalParsedEntityDecl,
	     (void *userData, const XML_Char *entityName,
	      const XML_Char *replacementText, int replacementTextLength),
	     ("(O&O&i)", STRING_CONV_FUNC, entityName,
	      STRING_CONV_FUNC, replacementText, replacementTextLength))

#endif /* Expat version 1.2 & better */

/* ---------------------------------------------------------------- */

static PyObject *
xmlparse_Parse(xmlparseobject *self, PyObject *args)
{
    char *s;
    int slen;
    int isFinal = 0;
    int rv;

    if (!PyArg_ParseTuple(args, "s#|i:Parse", &s, &slen, &isFinal))
        return NULL;
    rv = XML_Parse(self->itself, s, slen, isFinal);
    if (PyErr_Occurred()) {	
        return NULL;
    }
    else if (rv == 0) {
        return set_error(self);
    }
    return PyInt_FromLong(rv);
}

/* File reading copied from cPickle */

#define BUF_SIZE 2048

static int
readinst(char *buf, int buf_size, PyObject *meth)
{
    PyObject *arg = NULL;
    PyObject *bytes = NULL;
    PyObject *str = NULL;
    int len = -1;

    if ((bytes = PyInt_FromLong(buf_size)) == NULL)
        goto finally;

    if ((arg = PyTuple_New(1)) == NULL)
        goto finally;

    PyTuple_SET_ITEM(arg, 0, bytes);

    if ((str = PyObject_CallObject(meth, arg)) == NULL)
        goto finally;

    /* XXX what to do if it returns a Unicode string? */
    if (!PyString_Check(str)) {
        PyErr_Format(PyExc_TypeError, 
                     "read() did not return a string object (type=%.400s)",
                     str->ob_type->tp_name);
        goto finally;
    }
    len = PyString_GET_SIZE(str);
    if (len > buf_size) {
        PyErr_Format(PyExc_ValueError,
                     "read() returned too much data: "
                     "%i bytes requested, %i returned",
                     buf_size, len);
        Py_DECREF(str);
        goto finally;
    }
    memcpy(buf, PyString_AsString(str), len);
finally:
    Py_XDECREF(arg);
    Py_XDECREF(str);
    return len;
}

static PyObject *
xmlparse_ParseFile(xmlparseobject *self, PyObject *args)
{
    int rv = 1;
    PyObject *f;
    FILE *fp;
    PyObject *readmethod = NULL;

    if (!PyArg_ParseTuple(args, "O:ParseFile", &f))
        return NULL;

    if (PyFile_Check(f)) {
        fp = PyFile_AsFile(f);
    }
    else{
        fp = NULL;
        readmethod = PyObject_GetAttrString(f, "read");
        if (readmethod == NULL) {
            PyErr_Clear();
            PyErr_SetString(PyExc_TypeError, 
                            "argument must have 'read' attribute");
            return 0;
        }
    }
    for (;;) {
        int bytes_read;
        void *buf = XML_GetBuffer(self->itself, BUF_SIZE);
        if (buf == NULL)
            return PyErr_NoMemory();

        if (fp) {
            bytes_read = fread(buf, sizeof(char), BUF_SIZE, fp);
            if (bytes_read < 0) {
                PyErr_SetFromErrno(PyExc_IOError);
                return NULL;
            }
        }
        else {
            bytes_read = readinst((char *) buf, BUF_SIZE, readmethod);
            if (bytes_read < 0)
                return NULL;
        }
        rv = XML_ParseBuffer(self->itself, bytes_read, bytes_read == 0);
        if (PyErr_Occurred())
            return NULL;

        if (!rv || bytes_read == 0)
            break;
    }
    if (rv == 0) {
        return set_error(self);
    }
    return Py_BuildValue("i", rv);
}

static PyObject *
xmlparse_SetBase(xmlparseobject *self, PyObject *args)
{
    char *base;

    if (!PyArg_ParseTuple(args, "s:SetBase", &base))
        return NULL;
    if (!XML_SetBase(self->itself, base)) {
	return PyErr_NoMemory();
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
xmlparse_GetBase(xmlparseobject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":GetBase"))
        return NULL;

    return Py_BuildValue("z", XML_GetBase(self->itself));
}

#if EXPAT_VERSION >= 0x015f00

static PyObject *
xmlparse_GetInputContext(xmlparseobject *self, PyObject *args)
{
    PyObject *result = NULL;

    if (PyArg_ParseTuple(args, ":GetInputContext")) {
        if (self->in_callback) {
            int offset, size;
            const char *buffer
                = XML_GetInputContext(self->itself, &offset, &size);

            if (buffer != NULL)
                result = PyString_FromStringAndSize(buffer + offset, size);
            else {
                result = Py_None;
                Py_INCREF(result);
            }
        }
        else {
            result = Py_None;
            Py_INCREF(result);
        }
    }
    return result;
}
#endif

static PyObject *
xmlparse_ExternalEntityParserCreate(xmlparseobject *self, PyObject *args)
{
    char *context;
    char *encoding = NULL;
    xmlparseobject *new_parser;
    int i;

    if (!PyArg_ParseTuple(args, "z|s:ExternalEntityParserCreate",
                          &context, &encoding)) {
        return NULL;
    }

#if PY_MAJOR_VERSION == 1 && PY_MINOR_VERSION < 6
    new_parser = PyObject_NEW(xmlparseobject, Xmlparsetype);
#else
#ifndef Py_TPFLAGS_HAVE_GC
    /* Python versions 1.6 to 2.1 */
    new_parser = PyObject_New(xmlparseobject, Xmlparsetype);
#else
    /* Python versions 2.2 and later */
    new_parser = PyObject_GC_New(xmlparseobject, Xmlparsetype);
#endif
#endif

    if (new_parser == NULL)
        return NULL;
    new_parser->returns_unicode = self->returns_unicode;
    new_parser->ordered_attributes = self->ordered_attributes;
    new_parser->specified_attributes = self->specified_attributes;
    new_parser->in_callback = 0;
    new_parser->itself = XML_ExternalEntityParserCreate(self->itself, context,
							encoding);
    new_parser->handlers = 0;
    new_parser->tb_code = 0;
#ifdef Py_TPFLAGS_HAVE_GC
    PyObject_GC_Track(new_parser);
#else
    PyObject_GC_Init(new_parser);
#endif

    if (!new_parser->itself) {
        Py_DECREF(new_parser);
        return PyErr_NoMemory();
    }

    XML_SetUserData(new_parser->itself, (void *)new_parser);

    /* allocate and clear handlers first */
    for(i = 0; handler_info[i].name != NULL; i++)
        /* do nothing */;

    new_parser->handlers = (PyObject **) malloc(sizeof(PyObject *)*i);
    if (!new_parser->handlers) {
        Py_DECREF(new_parser);
        return PyErr_NoMemory();
    }

    new_parser->tb_code = (PyCodeObject **) calloc(i, sizeof(PyCodeObject *));
    if (!new_parser->tb_code) {
      free(new_parser->handlers);
      new_parser->handlers = NULL;
      Py_DECREF(new_parser);
      return PyErr_NoMemory();
    }

    clear_handlers(new_parser, 1);

    /* then copy handlers from self */
    for (i = 0; handler_info[i].name != NULL; i++) {
        if (self->handlers[i]) {
            Py_INCREF(self->handlers[i]);
            new_parser->handlers[i] = self->handlers[i];
            handler_info[i].setter(new_parser->itself, 
                                   handler_info[i].handler);
        }
    }
    return (PyObject *)new_parser;    
}

#if EXPAT_VERSION >= 0x010200

static PyObject*
xmlparse_SetParamEntityParsing(xmlparseobject *p, PyObject* args)
{
    int flag;
    if (!PyArg_ParseTuple(args, "i", &flag))
        return NULL;
    flag = (int) XML_SetParamEntityParsing(p->itself, 
					   (enum XML_ParamEntityParsing) flag);
    return PyInt_FromLong(flag);
}

#endif /* Expat version 1.2 or better */

static const struct PyMethodDef xmlparse_methods[] = {
    {"Parse",	  (PyCFunction)xmlparse_Parse,
     METH_VARARGS,	0 /* xmlparse_Parse__doc__ */},
    {"ParseFile", (PyCFunction)xmlparse_ParseFile,
     METH_VARARGS,	0 /* xmlparse_ParseFile__doc__ */},
    {"SetBase",   (PyCFunction)xmlparse_SetBase,
     METH_VARARGS, 0 /* xmlparse_SetBase__doc__ */},
    {"GetBase",   (PyCFunction)xmlparse_GetBase,
     METH_VARARGS, 0 /* xmlparse_GetBase__doc__ */},
    {"ExternalEntityParserCreate", (PyCFunction)xmlparse_ExternalEntityParserCreate,
     METH_VARARGS, 0 /* xmlparse_ExternalEntityParserCreate__doc__ */},
#if EXPAT_VERSION >= 0x010200
    {"SetParamEntityParsing", (PyCFunction)xmlparse_SetParamEntityParsing,
     METH_VARARGS, 0 /* xmlparse_SetParamEntityParsing__doc__ */},
#endif
#if EXPAT_VERSION >= 0x015f00
    {"GetInputContext", (PyCFunction)xmlparse_GetInputContext,
     METH_VARARGS, 0 /* xmlparse_GetInputContext__doc__ */},
#endif
	{NULL,		NULL}		/* sentinel */
};

/* ---------- */


#ifdef Py_USING_UNICODE

/* 
    pyexpat international encoding support.
    Make it as simple as possible.
*/

static const char template_buffer[] = {
  '\x00', '\x01', '\x02', '\x03', '\x04', '\x05', '\x06', '\x07',
  '\x08', '\x09', '\x0A', '\x0B', '\x0C', '\x0D', '\x0E', '\x0F',
  '\x10', '\x11', '\x12', '\x13', '\x14', '\x15', '\x16', '\x17',
  '\x18', '\x19', '\x1A', '\x1B', '\x1C', '\x1D', '\x1E', '\x1F',
  '\x20', '\x21', '\x22', '\x23', '\x24', '\x25', '\x26', '\x27',
  '\x28', '\x29', '\x2A', '\x2B', '\x2C', '\x2D', '\x2E', '\x2F',
  '\x30', '\x31', '\x32', '\x33', '\x34', '\x35', '\x36', '\x37',
  '\x38', '\x39', '\x3A', '\x3B', '\x3C', '\x3D', '\x3E', '\x3F',
  '\x40', '\x41', '\x42', '\x43', '\x44', '\x45', '\x46', '\x47',
  '\x48', '\x49', '\x4A', '\x4B', '\x4C', '\x4D', '\x4E', '\x4F',
  '\x50', '\x51', '\x52', '\x53', '\x54', '\x55', '\x56', '\x57',
  '\x58', '\x59', '\x5A', '\x5B', '\x5C', '\x5D', '\x5E', '\x5F',
  '\x60', '\x61', '\x62', '\x63', '\x64', '\x65', '\x66', '\x67',
  '\x68', '\x69', '\x6A', '\x6B', '\x6C', '\x6D', '\x6E', '\x6F',
  '\x70', '\x71', '\x72', '\x73', '\x74', '\x75', '\x76', '\x77',
  '\x78', '\x79', '\x7A', '\x7B', '\x7C', '\x7D', '\x7E', '\x7F',
  '\x80', '\x81', '\x82', '\x83', '\x84', '\x85', '\x86', '\x87',
  '\x88', '\x89', '\x8A', '\x8B', '\x8C', '\x8D', '\x8E', '\x8F',
  '\x90', '\x91', '\x92', '\x93', '\x94', '\x95', '\x96', '\x97',
  '\x98', '\x99', '\x9A', '\x9B', '\x9C', '\x9D', '\x9E', '\x9F',
  '\xA0', '\xA1', '\xA2', '\xA3', '\xA4', '\xA5', '\xA6', '\xA7',
  '\xA8', '\xA9', '\xAA', '\xAB', '\xAC', '\xAD', '\xAE', '\xAF',
  '\xB0', '\xB1', '\xB2', '\xB3', '\xB4', '\xB5', '\xB6', '\xB7',
  '\xB8', '\xB9', '\xBA', '\xBB', '\xBC', '\xBD', '\xBE', '\xBF',
  '\xC0', '\xC1', '\xC2', '\xC3', '\xC4', '\xC5', '\xC6', '\xC7',
  '\xC8', '\xC9', '\xCA', '\xCB', '\xCC', '\xCD', '\xCE', '\xCF',
  '\xD0', '\xD1', '\xD2', '\xD3', '\xD4', '\xD5', '\xD6', '\xD7',
  '\xD8', '\xD9', '\xDA', '\xDB', '\xDC', '\xDD', '\xDE', '\xDF',
  '\xE0', '\xE1', '\xE2', '\xE3', '\xE4', '\xE5', '\xE6', '\xE7',
  '\xE8', '\xE9', '\xEA', '\xEB', '\xEC', '\xED', '\xEE', '\xEF',
  '\xF0', '\xF1', '\xF2', '\xF3', '\xF4', '\xF5', '\xF6', '\xF7',
  '\xF8', '\xF9', '\xFA', '\xFB', '\xFC', '\xFD', '\xFE', '\xFF', 
  '\x00'
};


int 
PyUnknownEncodingHandler(void * /*encodingHandlerData*/, 
const XML_Char *name, 
XML_Encoding * info)
{
    PyUnicodeObject *_u_string = NULL;
    int result = 0;
    int i;
    
    /* Yes, supports only 8bit encodings */
    _u_string = (PyUnicodeObject *)
        PyUnicode_Decode(template_buffer, 256, name, "replace");
    
    if (_u_string == NULL)
	return result;
    
    for (i = 0; i < 256; i++) {
	/* Stupid to access directly, but fast */
	Py_UNICODE c = _u_string->str[i];
	if (c == Py_UNICODE_REPLACEMENT_CHARACTER)
	    info->map[i] = -1;
	else
	    info->map[i] = c;
    }
    
    info->data = NULL;
    info->convert = NULL;
    info->release = NULL;
    result=1;
    
    Py_DECREF(_u_string);
    return result;
}

#endif

static PyObject *
// XXX could be removed
newxmlparseobject(PyObject* /*module*/, char *encoding, 
		  char *namespace_separator)
{
    int i;
    xmlparseobject *self;

#if PY_MAJOR_VERSION == 1 && PY_MINOR_VERSION < 6
    self = PyObject_NEW(xmlparseobject, Xmlparsetype);
    if (self == NULL)
        return NULL;

    self->returns_unicode = 0;
#else
    /* Code for versions 1.6 and later */
#ifdef Py_TPFLAGS_HAVE_GC
    /* Code for versions 2.2 and later */
    self = PyObject_GC_New(xmlparseobject, Xmlparsetype);
#else
    self = PyObject_New(xmlparseobject, Xmlparsetype);
#endif
    if (self == NULL)
        return NULL;

    self->returns_unicode = 1;
#endif
    self->ErrorObject = //PyObject_GetAttrString(module, "error");
	SPyGetGlobalString("pyexpaterror");
    self->ordered_attributes = 0;
    self->specified_attributes = 0;
    self->in_callback = 0;
    self->handlers = NULL;
    self->tb_code = NULL;
    if (namespace_separator != NULL) {
        self->itself = XML_ParserCreateNS(encoding, *namespace_separator);
    }
    else {
        self->itself = XML_ParserCreate(encoding);
    }
#ifdef Py_TPFLAGS_HAVE_GC
    PyObject_GC_Track(self);
#else
    PyObject_GC_Init(self);
#endif
    if (self->itself == NULL) {
        PyErr_SetString(PyExc_RuntimeError, 
                        "XML_ParserCreate failed");
        Py_DECREF(self);
        return NULL;
    }
    XML_SetUserData(self->itself, (void *)self);
#ifdef Py_USING_UNICODE
    XML_SetUnknownEncodingHandler(self->itself, (XML_UnknownEncodingHandler) PyUnknownEncodingHandler, NULL);
#endif

    for(i = 0; handler_info[i].name != NULL; i++)
        /* do nothing */;

    self->handlers = (PyObject **) malloc(sizeof(PyObject *)*i);
    if (!self->handlers){
      Py_DECREF(self);
      return PyErr_NoMemory();
    }

    self->tb_code = (PyCodeObject **) calloc(i, sizeof(PyCodeObject *));
    if (!self->tb_code){
      free(self->handlers);
      self->handlers = NULL;
      Py_DECREF(self);
      return PyErr_NoMemory();
    }

    clear_handlers(self, 1);

    return (PyObject*)self;
}


static void
xmlparse_dealloc(xmlparseobject *self)
{
    int i;
#ifdef Py_TPFLAGS_HAVE_GC
    PyObject_GC_UnTrack(self);
#else
    PyObject_GC_Fini(self);
#endif
    if (self->itself != NULL)
        XML_ParserFree(self->itself);
    self->itself = NULL;

    if (self->handlers != NULL) {
        PyObject *temp;
        for (i = 0; handler_info[i].name != NULL; i++) {
            temp = self->handlers[i];
            self->handlers[i] = NULL;
            Py_XDECREF(temp);
        }
        free(self->handlers);
	self->handlers = NULL;
    }
    if (self->tb_code) {
      for (i = 0; handler_info[i].name; i++) {
	PyCodeObject *temp;
	if ((temp = self->tb_code[i]) != NULL) {
	  self->tb_code[i] = NULL;
	  Py_XDECREF(temp);
	}
      }
      free(self->tb_code);
      self->tb_code = NULL;
    }
#if PY_MAJOR_VERSION == 1 && PY_MINOR_VERSION < 6
    /* Code for versions before 1.6 */
    free(self);
#else
#ifndef Py_TPFLAGS_HAVE_GC
    /* Code for versions 1.6 to 2.1 */
    PyObject_Del(self);
#else
    /* Code for versions 2.2 and later. */
    PyObject_GC_Del(self);
#endif
#endif
}

static int
handlername2int(const char *name)
{
    int i;
    for (i=0; handler_info[i].name != NULL; i++) {
        if (strcmp(name, handler_info[i].name) == 0) {
            return i;
        }
    }
    return -1;
}

static PyObject *
xmlparse_getattr(xmlparseobject *self, char *name)
{
    int handlernum;
    if (strcmp(name, "ErrorCode") == 0)
        return PyInt_FromLong((long) XML_GetErrorCode(self->itself));
    if (strcmp(name, "ErrorLineNumber") == 0)
        return PyInt_FromLong((long) XML_GetErrorLineNumber(self->itself));
    if (strcmp(name, "ErrorColumnNumber") == 0)
        return PyInt_FromLong((long) XML_GetErrorColumnNumber(self->itself));
    if (strcmp(name, "ErrorByteIndex") == 0)
        return PyInt_FromLong((long) XML_GetErrorByteIndex(self->itself));
    if (strcmp(name, "ordered_attributes") == 0)
        return PyInt_FromLong((long) self->ordered_attributes);
    if (strcmp(name, "returns_unicode") == 0)
        return PyInt_FromLong((long) self->returns_unicode);
    if (strcmp(name, "specified_attributes") == 0)
        return PyInt_FromLong((long) self->specified_attributes);

    handlernum = handlername2int(name);

    if (handlernum != -1 && self->handlers[handlernum] != NULL) {
        Py_INCREF(self->handlers[handlernum]);
        return self->handlers[handlernum];
    }

#define APPEND(list, str)				\
	{						\
		PyObject *o = PyString_FromString(str);	\
		if (o != NULL)				\
			PyList_Append(list, o);		\
		Py_XDECREF(o);				\
	}

    if (strcmp(name, "__members__") == 0) {
        int i;
        PyObject *rc = PyList_New(0);
        for (i = 0; handler_info[i].name != NULL; i++) {
            APPEND(rc, handler_info[i].name);
        }
        APPEND(rc, "ErrorCode");
        APPEND(rc, "ErrorLineNumber");
        APPEND(rc, "ErrorColumnNumber");
        APPEND(rc, "ErrorByteIndex");
        APPEND(rc, "ordered_attributes");
        APPEND(rc, "returns_unicode");
        APPEND(rc, "specified_attributes");

#undef APPEND
        return rc;
    }
    /* Unfortunately the Python/C API does not have all the right
       consts, but judging from the code of Py_FindMethod,
       const_casting the first parameter here is okay, at least
       with the version of Python that we are building against.
     */
    return Py_FindMethod((PyMethodDef *)xmlparse_methods,
			 (PyObject *)self, name);
}

static int
sethandler(xmlparseobject *self, const char *name, PyObject* v)
{
    int handlernum = handlername2int(name);
    if (handlernum != -1) {
        Py_INCREF(v);
        Py_XDECREF(self->handlers[handlernum]);
        self->handlers[handlernum] = v;
        handler_info[handlernum].setter(self->itself, 
                                        handler_info[handlernum].handler);
        return 1;
    }
    return 0;
}

static int
xmlparse_setattr(xmlparseobject *self, char *name, PyObject *v)
{
    /* Set attribute 'name' to value 'v'. v==NULL means delete */
    if (v == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Cannot delete attribute");
        return -1;
    }
    if (strcmp(name, "ordered_attributes") == 0) {
        if (PyObject_IsTrue(v))
            self->ordered_attributes = 1;
        else
            self->ordered_attributes = 0;
        return 0;
    }
    if (strcmp(name, "returns_unicode") == 0) {
        if (PyObject_IsTrue(v)) {
#ifndef Py_USING_UNICODE
            PyErr_SetString(PyExc_ValueError, 
                            "Cannot return Unicode strings in Python 1.5");
            return -1;
#else
            self->returns_unicode = 1;
#endif
        }
        else
            self->returns_unicode = 0;
        return 0;
    }
    if (strcmp(name, "specified_attributes") == 0) {
        if (PyObject_IsTrue(v))
            self->specified_attributes = 1;
        else
            self->specified_attributes = 0;
        return 0;
    }
    if (sethandler(self, name, v)) {
        return 0;
    }
    PyErr_SetString(PyExc_AttributeError, name);
    return -1;
}

#ifdef WITH_CYCLE_GC
static int
xmlparse_traverse(xmlparseobject *op, visitproc visit, void *arg)
{
    int i, err;
    for (i = 0; handler_info[i].name != NULL; i++) {
        if (!op->handlers[i])
            continue;
        err = visit(op->handlers[i], arg);
        if (err)
            return err;
    }
    return 0;
}

static int
xmlparse_clear(xmlparseobject *op)
{
    clear_handlers(op, 0);
    return 0;
}
#endif

static const PyTypeObject Xmlparsetype_template = {
    PyObject_HEAD_INIT(NULL)
	0,				/*ob_size*/
	"pyexpat.xmlparser",		/*tp_name*/
	sizeof(xmlparseobject) + PyGC_HEAD_SIZE,/*tp_basicsize*/
	0,				/*tp_itemsize*/
	/* methods */
	(destructor)xmlparse_dealloc,	/*tp_dealloc*/
	(printfunc)0,		/*tp_print*/
	(getattrfunc)xmlparse_getattr,	/*tp_getattr*/
	(setattrfunc)xmlparse_setattr,	/*tp_setattr*/
	(cmpfunc)0,		/*tp_compare*/
	(reprfunc)0,		/*tp_repr*/
	0,			/*tp_as_number*/
	0,		/*tp_as_sequence*/
	0,		/*tp_as_mapping*/
	(hashfunc)0,		/*tp_hash*/
	(ternaryfunc)0,		/*tp_call*/
	(reprfunc)0,		/*tp_str*/
	0,		/* tp_getattro */
	0,		/* tp_setattro */
	0,		/* tp_as_buffer */
#ifdef Py_TPFLAGS_HAVE_GC
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC, /*tp_flags*/	
#else
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_GC, /*tp_flags*/	
#endif
	0, /* Xmlparsetype__doc__, */ /* Documentation string */
#ifdef WITH_CYCLE_GC
	(traverseproc)xmlparse_traverse,	/* tp_traverse */
	(inquiry)xmlparse_clear		/* tp_clear */
#else
	0, 0
#endif
};

/* End of code for xmlparser objects */
/* -------------------------------------------------------- */

// the first parameter is always 0 for functions,
// and only set for methods
static PyObject *
pyexpat_ParserCreate(PyObject* /*module*/, PyObject *args, PyObject *kw)
{
    char *encoding = NULL;
    char *namespace_separator = NULL;
    char *kwlist[] = {"encoding", "namespace_separator", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kw, "|zz:ParserCreate", kwlist,
                                     &encoding, &namespace_separator))
        return NULL;
    if (namespace_separator != NULL
        && strlen(namespace_separator) > 1) {
        PyErr_SetString(PyExc_ValueError,
                        "namespace_separator must be at most one"
                        " character, omitted, or None");
        return NULL;
    }
    return newxmlparseobject(NULL /*module*/, encoding, namespace_separator);
}

static PyObject *
pyexpat_ErrorString(PyObject * /*self*/, PyObject *args)
{
    long code = 0;

    if (!PyArg_ParseTuple(args, "l:ErrorString", &code))
        return NULL;
    return Py_BuildValue("z", XML_ErrorString((enum XML_Error) code));
}

/* List of methods defined in the module */

static const struct PyMethodDef pyexpat_methods[] = {
    {"ParserCreate",	(PyCFunction)pyexpat_ParserCreate,
     METH_VARARGS|METH_KEYWORDS, 0 /* pyexpat_ParserCreate__doc__ */},
    {"ErrorString",	(PyCFunction)pyexpat_ErrorString,
     METH_VARARGS, 0 /* pyexpat_ErrorString__doc__ */},
 
    {NULL,	 (PyCFunction)NULL, 0, NULL}		/* sentinel */
};

/* Module docstring */

#if PY_VERSION_HEX < 0x20000F0

/* 1.5 compatibility: PyModule_AddObject */
static int
PyModule_AddObject(PyObject *m, char *name, PyObject *o)
{
    PyObject *dict;
    if (!PyModule_Check(m) || o == NULL)
        return -1;
    dict = PyModule_GetDict(m);
    if (dict == NULL)
        return -1;
    if (PyDict_SetItemString(dict, name, o))
        return -1;
    Py_DECREF(o);
    return 0;
}

int 
PyModule_AddIntConstant(PyObject *m, char *name, long value)
{
    return PyModule_AddObject(m, name, PyInt_FromLong(value));
}

static int 
PyModule_AddStringConstant(PyObject *m, char *name, char *value)
{
    return PyModule_AddObject(m, name, PyString_FromString(value));
}

#endif


/* Return a Python string that represents the version number without the
 * extra cruft added by revision control, even if the right options were
 * given to the "cvs export" command to make it not include the extra
 * cruft.
 */
static PyObject *
get_version_string(void)
{
    const char *rcsid = "$Revision: 2.57.6.6 $";
    const char *rev = rcsid;
    int i = 0;

    while (!isdigit(*rev))
        ++rev;
    while (rev[i] != ' ' && rev[i] != '\0')
        ++i;

    return PyString_FromStringAndSize(rev, i);
}

/* Initialization function for the module */

#ifndef MODULE_NAME
#define MODULE_NAME "pyexpat"
#else
#error was defined
#endif

#ifndef MODULE_INITFUNC
#define MODULE_INITFUNC initpyexpat
#endif

//void Log(const char* aText); // from logging.cpp

DL_EXPORT(void) MODULE_INITFUNC(void)
{
    PyObject *m, *d;
    PyObject *errmod_name = PyString_FromString(MODULE_NAME ".errors");
    PyObject *errors_module;
    PyObject *modelmod_name;
    PyObject *model_module;
    PyObject *sys_modules;
    PyObject *ErrorObject;
    PyTypeObject *xmlobj;

    //Log("initpyexpat()");

    /* Create the module and add the functions */
    /* Unfortunately the Python/C API does not have all the right
       consts, but judging from the code of Py_InitModule4,
       const_casting the second parameter here is okay, at least
       with the version of Python that we are building against.
     */
    m = Py_InitModule(MODULE_NAME, (PyMethodDef *) pyexpat_methods);
    /* pyexpat_module_documentation */

    xmlobj = PyObject_New(PyTypeObject, &PyType_Type);
    *xmlobj = Xmlparsetype_template;
    xmlobj->ob_type = &PyType_Type;
    /*
    if (PyType_Ready(xmlobj) < 0)
	return;
    */
    SPyAddGlobalString("Xmlparsetype", (PyObject*)xmlobj);
    PyModule_AddObject(m, "XMLParserType", (PyObject *) Xmlparsetype);

    if (errmod_name == NULL)
        return;
    modelmod_name = PyString_FromString(MODULE_NAME ".model");
    if (modelmod_name == NULL)
        return;

    /* Add some symbolic constants to the module */
    ErrorObject = PyErr_NewException("xml.parsers.expat.ExpatError",
				     NULL, NULL);
    if (ErrorObject == NULL)
      return;
    Py_INCREF(ErrorObject);
    //PyModule_AddObject(m, "error", ErrorObject);
    SPyAddGlobalString("pyexpaterror", ErrorObject);
    Py_INCREF(ErrorObject);
    PyModule_AddObject(m, "ExpatError", ErrorObject);
    /* Py_INCREF(&Xmlparsetype); I think this is unnecessary */
	
    PyModule_AddObject(m, "__version__", get_version_string());
#if EXPAT_VERSION >= 0x015f02
    PyModule_AddStringConstant(m, "EXPAT_VERSION",
                               (char *) XML_ExpatVersion());
    {
        XML_Expat_Version info = XML_ExpatVersionInfo();
        PyModule_AddObject(m, "version_info",
                           Py_BuildValue("(iii)", info.major,
                                         info.minor, info.micro));
    }
#endif
    /* XXX When Expat supports some way of figuring out how it was
       compiled, this should check and set native_encoding 
       appropriately. 
    */
    PyModule_AddStringConstant(m, "native_encoding", "UTF-8");

    sys_modules = PySys_GetObject("modules");
    d = PyModule_GetDict(m);
    errors_module = PyDict_GetItem(d, errmod_name);
    if (errors_module == NULL) {
        errors_module = PyModule_New(MODULE_NAME ".errors");
        if (errors_module != NULL) {
            PyDict_SetItem(sys_modules, errmod_name, errors_module);
            /* gives away the reference to errors_module */
            PyModule_AddObject(m, "errors", errors_module);
        }
    }
    Py_DECREF(errmod_name);
    model_module = PyDict_GetItem(d, modelmod_name);
    if (model_module == NULL) {
        model_module = PyModule_New(MODULE_NAME ".model");
        if (model_module != NULL) {
            PyDict_SetItem(sys_modules, modelmod_name, model_module);
            /* gives away the reference to model_module */
            PyModule_AddObject(m, "model", model_module);
        }
    }
    Py_DECREF(modelmod_name);
    if (errors_module == NULL || model_module == NULL)
        /* Don't core dump later! */
        return;

#define MYCONST(name) \
    PyModule_AddStringConstant(errors_module, #name, \
                               (char*)XML_ErrorString(name))

    MYCONST(XML_ERROR_NO_MEMORY);
    MYCONST(XML_ERROR_SYNTAX);
    MYCONST(XML_ERROR_NO_ELEMENTS);
    MYCONST(XML_ERROR_INVALID_TOKEN);
    MYCONST(XML_ERROR_UNCLOSED_TOKEN);
    MYCONST(XML_ERROR_PARTIAL_CHAR);
    MYCONST(XML_ERROR_TAG_MISMATCH);
    MYCONST(XML_ERROR_DUPLICATE_ATTRIBUTE);
    MYCONST(XML_ERROR_JUNK_AFTER_DOC_ELEMENT);
    MYCONST(XML_ERROR_PARAM_ENTITY_REF);
    MYCONST(XML_ERROR_UNDEFINED_ENTITY);
    MYCONST(XML_ERROR_RECURSIVE_ENTITY_REF);
    MYCONST(XML_ERROR_ASYNC_ENTITY);
    MYCONST(XML_ERROR_BAD_CHAR_REF);
    MYCONST(XML_ERROR_BINARY_ENTITY_REF);
    MYCONST(XML_ERROR_ATTRIBUTE_EXTERNAL_ENTITY_REF);
    MYCONST(XML_ERROR_MISPLACED_XML_PI);
    MYCONST(XML_ERROR_UNKNOWN_ENCODING);
    MYCONST(XML_ERROR_INCORRECT_ENCODING);
    MYCONST(XML_ERROR_UNCLOSED_CDATA_SECTION);
    MYCONST(XML_ERROR_EXTERNAL_ENTITY_HANDLING);
    MYCONST(XML_ERROR_NOT_STANDALONE);

    PyModule_AddStringConstant(errors_module, "__doc__",
                               "Constants used to describe error conditions.");

#undef MYCONST

#if EXPAT_VERSION >= 0x010200
#define MYCONST(c) PyModule_AddIntConstant(m, #c, c)
    MYCONST(XML_PARAM_ENTITY_PARSING_NEVER);
    MYCONST(XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE);
    MYCONST(XML_PARAM_ENTITY_PARSING_ALWAYS);
#undef MYCONST
#endif

#if EXPAT_VERSION >= 0x015f00
#define MYCONST(c) PyModule_AddIntConstant(model_module, #c, c)
    PyModule_AddStringConstant(model_module, "__doc__",
                     "Constants used to interpret content model information.");

    MYCONST(XML_CTYPE_EMPTY);
    MYCONST(XML_CTYPE_ANY);
    MYCONST(XML_CTYPE_MIXED);
    MYCONST(XML_CTYPE_NAME);
    MYCONST(XML_CTYPE_CHOICE);
    MYCONST(XML_CTYPE_SEQ);

    MYCONST(XML_CQUANT_NONE);
    MYCONST(XML_CQUANT_OPT);
    MYCONST(XML_CQUANT_REP);
    MYCONST(XML_CQUANT_PLUS);
#undef MYCONST
#endif
}


static void
clear_handlers(xmlparseobject *self, int initial)
{
    int i = 0;
    PyObject *temp;

    for (; handler_info[i].name!=NULL; i++) {
        if (initial)
	    self->handlers[i]=NULL;
	else {
            temp = self->handlers[i];
            self->handlers[i] = NULL;
            Py_XDECREF(temp);
	    handler_info[i].setter(self->itself, NULL);
        }
    }
}

typedef void (*pairsetter)(XML_Parser, void *handler1, void *handler2);

static void
pyxml_UpdatePairedHandlers(xmlparseobject *self, 
                           int startHandler, 
                           int endHandler,
                           pairsetter setter)
{
    void *start_handler = NULL;
    void *end_handler = NULL;

    if (self->handlers[startHandler]
        && self->handlers[startHandler] != Py_None) {
        start_handler = handler_info[startHandler].handler;
    }
    if (self->handlers[endHandler]
        && self->handlers[endHandler] != Py_None) {
        end_handler = handler_info[endHandler].handler;
    }
    setter(self->itself, start_handler, end_handler);
}

static void
pyxml_SetStartElementHandler(XML_Parser parser, void * /*junk*/)
{
    pyxml_UpdatePairedHandlers((xmlparseobject *)XML_GetUserData(parser),
                               StartElement, EndElement,
                               (pairsetter)XML_SetElementHandler);
}

static void
pyxml_SetEndElementHandler(XML_Parser parser, void * /*junk*/)
{
    pyxml_UpdatePairedHandlers((xmlparseobject *)XML_GetUserData(parser), 
                               StartElement, EndElement,
                               (pairsetter)XML_SetElementHandler);
}

static void
pyxml_SetStartNamespaceDeclHandler(XML_Parser parser, void * /*junk*/)
{
    pyxml_UpdatePairedHandlers((xmlparseobject *)XML_GetUserData(parser), 
                               StartNamespaceDecl, EndNamespaceDecl,
                               (pairsetter)XML_SetNamespaceDeclHandler);
}

static void
pyxml_SetEndNamespaceDeclHandler(XML_Parser parser, void * /*junk*/)
{
    pyxml_UpdatePairedHandlers((xmlparseobject *)XML_GetUserData(parser), 
                               StartNamespaceDecl, EndNamespaceDecl,
                               (pairsetter)XML_SetNamespaceDeclHandler);
}

static void
pyxml_SetStartCdataSection(XML_Parser parser, void * /*junk*/)
{
    pyxml_UpdatePairedHandlers((xmlparseobject *)XML_GetUserData(parser),
                               StartCdataSection, EndCdataSection,
                               (pairsetter)XML_SetCdataSectionHandler);
}

static void
pyxml_SetEndCdataSection(XML_Parser parser, void * /*junk*/)
{
    pyxml_UpdatePairedHandlers((xmlparseobject *)XML_GetUserData(parser), 
                               StartCdataSection, EndCdataSection, 
                               (pairsetter)XML_SetCdataSectionHandler);
}

#if EXPAT_VERSION >= 0x010200

static void
pyxml_SetStartDoctypeDeclHandler(XML_Parser parser, void * /*junk*/)
{
    pyxml_UpdatePairedHandlers((xmlparseobject *)XML_GetUserData(parser),
                               StartDoctypeDecl, EndDoctypeDecl,
                               (pairsetter)XML_SetDoctypeDeclHandler);
}

static void
pyxml_SetEndDoctypeDeclHandler(XML_Parser parser, void * /*junk*/)
{
    pyxml_UpdatePairedHandlers((xmlparseobject *)XML_GetUserData(parser),
                               StartDoctypeDecl, EndDoctypeDecl,
                               (pairsetter)XML_SetDoctypeDeclHandler);
}

#endif

const struct HandlerInfo handler_info[64] = {
    {"StartElementHandler", 
     pyxml_SetStartElementHandler, 
     (xmlhandler)my_StartElementHandler},
    {"EndElementHandler", 
     pyxml_SetEndElementHandler, 
     (xmlhandler)my_EndElementHandler},
    {"ProcessingInstructionHandler", 
     (xmlhandlersetter)XML_SetProcessingInstructionHandler,
     (xmlhandler)my_ProcessingInstructionHandler},
    {"CharacterDataHandler", 
     (xmlhandlersetter)XML_SetCharacterDataHandler,
     (xmlhandler)my_CharacterDataHandler},
    {"UnparsedEntityDeclHandler", 
     (xmlhandlersetter)XML_SetUnparsedEntityDeclHandler,
     (xmlhandler)my_UnparsedEntityDeclHandler },
    {"NotationDeclHandler", 
     (xmlhandlersetter)XML_SetNotationDeclHandler,
     (xmlhandler)my_NotationDeclHandler },
    {"StartNamespaceDeclHandler", 
     pyxml_SetStartNamespaceDeclHandler,
     (xmlhandler)my_StartNamespaceDeclHandler },
    {"EndNamespaceDeclHandler", 
     pyxml_SetEndNamespaceDeclHandler,
     (xmlhandler)my_EndNamespaceDeclHandler },
    {"CommentHandler",
     (xmlhandlersetter)XML_SetCommentHandler,
     (xmlhandler)my_CommentHandler},
    {"StartCdataSectionHandler",
     pyxml_SetStartCdataSection,
     (xmlhandler)my_StartCdataSectionHandler},
    {"EndCdataSectionHandler",
     pyxml_SetEndCdataSection,
     (xmlhandler)my_EndCdataSectionHandler},
    {"DefaultHandler",
     (xmlhandlersetter)XML_SetDefaultHandler,
     (xmlhandler)my_DefaultHandler},
    {"DefaultHandlerExpand",
     (xmlhandlersetter)XML_SetDefaultHandlerExpand,
     (xmlhandler)my_DefaultHandlerExpandHandler},
    {"NotStandaloneHandler",
     (xmlhandlersetter)XML_SetNotStandaloneHandler,
     (xmlhandler)my_NotStandaloneHandler},
    {"ExternalEntityRefHandler",
     (xmlhandlersetter)XML_SetExternalEntityRefHandler,
     (xmlhandler)my_ExternalEntityRefHandler },
#if EXPAT_VERSION >= 0x010200
    {"StartDoctypeDeclHandler",
     pyxml_SetStartDoctypeDeclHandler,
     (xmlhandler)my_StartDoctypeDeclHandler},
    {"EndDoctypeDeclHandler",
     pyxml_SetEndDoctypeDeclHandler,
     (xmlhandler)my_EndDoctypeDeclHandler},
#endif
#if EXPAT_VERSION == 0x010200
    {"ExternalParsedEntityDeclHandler",
     (xmlhandlersetter)XML_SetExternalParsedEntityDeclHandler,
     (xmlhandler)my_ExternalParsedEntityDeclHandler},
    {"InternalParsedEntityDeclHandler",
     (xmlhandlersetter)XML_SetInternalParsedEntityDeclHandler,
     (xmlhandler)my_InternalParsedEntityDeclHandler},
#endif
#if EXPAT_VERSION >= 0x015f00
    {"EntityDeclHandler",
     (xmlhandlersetter)XML_SetEntityDeclHandler,
     (xmlhandler)my_EntityDeclHandler},
    {"XmlDeclHandler",
     (xmlhandlersetter)XML_SetXmlDeclHandler,
     (xmlhandler)my_XmlDeclHandler},
    {"ElementDeclHandler",
     (xmlhandlersetter)XML_SetElementDeclHandler,
     (xmlhandler)my_ElementDeclHandler},
    {"AttlistDeclHandler",
     (xmlhandlersetter)XML_SetAttlistDeclHandler,
     (xmlhandler)my_AttlistDeclHandler},
#endif /* Expat version 1.95 or better */

    {NULL, NULL, NULL} /* sentinel */
};

}
