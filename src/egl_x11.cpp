/**
 * EGL windows desktop implementation.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) since 2014 Norbert Nopper
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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "egl_internal.h"
#include "../../EGL/include/EGL/eglctxinternals.h"
#include <iostream>
#include <thread>
#include <dlfcn.h>

typedef GLXContext (*__PFN_glXCreateContextAttribsARB)(Display*, GLXFBConfig,
                                                       GLXContext, Bool,
                                                       const int*);
typedef void (*__PFN_glXSwapIntervalEXT)(Display*, GLXDrawable, int);
typedef void(*__PFN_glFinish)();
typedef void* (*__PFN_glFenceSync)(GLenum condition, GLbitfield flags);
typedef void  (*__PFN_glDeleteSync)(void* sync);
typedef GLenum(*__PFN_glClientWaitSync)(void* sync, GLbitfield flags, unsigned long long timeout);
typedef void  (*__PFN_glWaitSync)(void* sync, GLbitfield flags, unsigned long long timeout);
typedef void  (*__PFN_glGetSynciv)(void* sync, GLenum pname, GLsizei count, GLsizei* length, GLint* values);
typedef void (*__PFN_glXBindTexImageEXT)(Display*, GLXDrawable, int, const int*);
typedef void (*__PFN_glXReleaseTexImageEXT)(Display*, GLXDrawable, int);

__PFN_glXCreateContextAttribsARB glXCreateContextAttribsARB_PTR = NULL;
__PFN_glXSwapIntervalEXT glXSwapIntervalEXT_PTR = NULL;
__PFN_glFinish glFinish_PTR = NULL;
__PFN_glFenceSync glFenceSync_PTR = NULL;
__PFN_glDeleteSync glDeleteSync_PTR = NULL;
__PFN_glClientWaitSync glClientWaitSync_PTR = NULL;
__PFN_glWaitSync glWaitSync_PTR = NULL;
__PFN_glGetSynciv glGetSynciv_PTR = NULL;
__PFN_glXBindTexImageEXT glXBindTexImageEXT_PTR = NULL;
__PFN_glXReleaseTexImageEXT glXReleaseTexImageEXT_PTR = NULL;

#define glXSwapIntervalEXT(...) glXSwapIntervalEXT_PTR(__VA_ARGS__)
#define glXCreateContextAttribsARB(...) \
    glXCreateContextAttribsARB_PTR(__VA_ARGS__)

void* libx11 = NULL;
void* libgl = NULL;
//X
decltype(XOpenDisplay)* XOpenDisplay_PTR = NULL;
decltype(XCloseDisplay)* XCloseDisplay_PTR = NULL;
decltype(XDestroyWindow)* XDestroyWindow_PTR = NULL;
decltype(XFree)* XFree_PTR = NULL;
decltype(XGetErrorText)* XGetErrorText_PTR = NULL;
decltype(XSetErrorHandler)* XSetErrorHandler_PTR = NULL;
decltype(XGetGeometry)* XGetGeometry_PTR = NULL;
decltype(XCreateImage)* XCreateImage_PTR = NULL;
decltype(XPutImage)* XPutImage_PTR = NULL;
decltype(XCreateGC)* XCreateGC_PTR = NULL;
decltype(XFreeGC)* XFreeGC_PTR = NULL;
decltype(XDefaultVisual)* XDefaultVisual_PTR = NULL;
decltype(XDefaultDepth)* XDefaultDepth_PTR = NULL;
//glX
decltype(glXGetProcAddress)* glXGetProcAddress_PTR = NULL;
Bool(*glXQueryVersion_PTR)(Display*,int*,int*) = NULL;
XVisualInfo*(*glXChooseVisual_PTR)(Display*,int,int*) = NULL;
GLXContext(*glXCreateContext_PTR)(Display*, XVisualInfo*, GLXContext, Bool);
Bool(*glXMakeCurrent_PTR)(Display*,GLXDrawable,GLXContext) = NULL;
void(*glXDestroyContext_PTR)(Display*,GLXContext) = NULL;
GLXFBConfig*(*glXChooseFBConfig_PTR)(Display*,int,const int*, int*) = NULL;
int(*glXGetFBConfigAttrib_PTR)(Display*,GLXFBConfig,int,int*) = NULL;
XVisualInfo*(*glXGetVisualFromFBConfig_PTR)(Display*,GLXFBConfig) = NULL;
void(*glXSwapBuffers_PTR)(Display*,GLXDrawable) = NULL;
GLXPbuffer(*glXCreatePbuffer_PTR)(Display*,GLXFBConfig,const int*) = NULL;
void(*glXDestroyPbuffer_PTR)(Display*,GLXPbuffer) = NULL;
const char*(*glXQueryExtensionsString_PTR)(Display*,int) = NULL;
GLXFBConfig*(*glXGetFBConfigs_PTR)(Display*,int,int*) = NULL;
Bool(*glXMakeContextCurrent_PTR)(Display*,GLXDrawable,GLXDrawable,GLXContext) = NULL;
GLXPixmap(*glXCreatePixmap_PTR)(Display*,GLXFBConfig,Pixmap,const int*) = NULL;
void(*glXDestroyPixmap_PTR)(Display*,GLXPixmap) = NULL;

__eglMustCastToProperFunctionPointerType __getProcAddress(const char *procname)
{
	return (__eglMustCastToProperFunctionPointerType) glXGetProcAddress_PTR((const GLubyte *)procname);
}

//#define DEBUG_EGL_X11_BACKEND

#ifdef DEBUG_EGL_X11_BACKEND
static void logglxcall(const char* fname)
{
	auto tid = std::this_thread::get_id();
	size_t t = std::hash<std::thread::id>{}(tid);
	std::cout << "tid=" << t << ": " << fname << std::endl;
}
#else
#	define logglxcall(fname)
#endif

EGLBoolean __internalInit(NativeLocalStorageContainer* nativeLocalStorageContainer, EGLint* GL_max_supported, EGLint* ES_max_supported)
{
	if (nativeLocalStorageContainer->display && nativeLocalStorageContainer->window && nativeLocalStorageContainer->ctx)
	{
		return EGL_TRUE;
	}

	if (nativeLocalStorageContainer->display)
	{
		return EGL_FALSE;
	}

	if (nativeLocalStorageContainer->window)
	{
		return EGL_FALSE;
	}

	if (nativeLocalStorageContainer->ctx)
	{
		return EGL_FALSE;
	}

	libx11 = dlopen("libX11.so", RTLD_LAZY);
	libgl = dlopen("libGL.so", RTLD_LAZY);
#define LOAD_X11_FUNC_PTR(fname) fname##_PTR = (decltype(fname##_PTR)) dlsym(libx11, #fname)
	LOAD_X11_FUNC_PTR(XOpenDisplay);
	LOAD_X11_FUNC_PTR(XCloseDisplay);
	LOAD_X11_FUNC_PTR(XDestroyWindow);
	LOAD_X11_FUNC_PTR(XFree);
	LOAD_X11_FUNC_PTR(XGetErrorText);
	LOAD_X11_FUNC_PTR(XSetErrorHandler);
	LOAD_X11_FUNC_PTR(XGetGeometry);
	LOAD_X11_FUNC_PTR(XCreateImage);
	LOAD_X11_FUNC_PTR(XPutImage);
	LOAD_X11_FUNC_PTR(XCreateGC);
	LOAD_X11_FUNC_PTR(XFreeGC);
	LOAD_X11_FUNC_PTR(XDefaultVisual);
	LOAD_X11_FUNC_PTR(XDefaultDepth);
	//LOAD_GLX_FUNC_PTR(glXGetProcAddress);
	glXGetProcAddress_PTR = (decltype(glXGetProcAddress_PTR)) dlsym(libgl, "glXGetProcAddress");
	if (!glXGetProcAddress_PTR)
		glXGetProcAddress_PTR = (decltype(glXGetProcAddress_PTR)) dlsym(libgl, "glXGetProcAddressARB");
#define LOAD_GLX_FUNC_PTR(fname) fname##_PTR = (decltype(fname##_PTR)) __getProcAddress(#fname);
	LOAD_GLX_FUNC_PTR(glXQueryVersion);
	LOAD_GLX_FUNC_PTR(glXChooseVisual);
	LOAD_GLX_FUNC_PTR(glXCreateContext);
	LOAD_GLX_FUNC_PTR(glXMakeCurrent);
	LOAD_GLX_FUNC_PTR(glXDestroyContext);
	LOAD_GLX_FUNC_PTR(glXChooseFBConfig);
	LOAD_GLX_FUNC_PTR(glXGetFBConfigAttrib);
	LOAD_GLX_FUNC_PTR(glXGetVisualFromFBConfig);
	LOAD_GLX_FUNC_PTR(glXSwapBuffers);
	LOAD_GLX_FUNC_PTR(glXCreatePbuffer);
	LOAD_GLX_FUNC_PTR(glXDestroyPbuffer);
	LOAD_GLX_FUNC_PTR(glXQueryExtensionsString);
	LOAD_GLX_FUNC_PTR(glXGetFBConfigs);
	LOAD_GLX_FUNC_PTR(glXMakeContextCurrent);
	LOAD_GLX_FUNC_PTR(glXCreatePixmap);
	LOAD_GLX_FUNC_PTR(glXDestroyPixmap);
	glXBindTexImageEXT_PTR = (__PFN_glXBindTexImageEXT)__getProcAddress("glXBindTexImageEXT");
	glXReleaseTexImageEXT_PTR = (__PFN_glXReleaseTexImageEXT)__getProcAddress("glXReleaseTexImageEXT");

	nativeLocalStorageContainer->display = XOpenDisplay_PTR(NULL);

	if (!nativeLocalStorageContainer->display)
	{
		return EGL_FALSE;
	}

	//

	int glxMajor;
	int glxMinor;

	// GLX version 1.4 or higher needed.
	logglxcall("glXQueryVersion");
	if (!glXQueryVersion_PTR(nativeLocalStorageContainer->display, &glxMajor, &glxMinor))
	{
		XCloseDisplay_PTR(nativeLocalStorageContainer->display);
		nativeLocalStorageContainer->display = 0;

		return EGL_FALSE;
	}

	if (glxMajor < 1 || (glxMajor == 1 && glxMinor < 4))
	{
		XCloseDisplay_PTR(nativeLocalStorageContainer->display);
		nativeLocalStorageContainer->display = 0;

		return EGL_FALSE;
	}

	//

	nativeLocalStorageContainer->window = DefaultRootWindow(nativeLocalStorageContainer->display);

	if (!nativeLocalStorageContainer->window)
	{
		XCloseDisplay_PTR(nativeLocalStorageContainer->display);
		nativeLocalStorageContainer->display = 0;

		return EGL_FALSE;
	}

	int dummyAttribList[] = {
		GLX_USE_GL, True,
		GLX_DOUBLEBUFFER, True,
		GLX_RGBA, True,
		None
	};

	logglxcall("glXChooseVisual");
	XVisualInfo* visualInfo = glXChooseVisual_PTR(nativeLocalStorageContainer->display, 0, dummyAttribList);

	if (!visualInfo)
	{
		nativeLocalStorageContainer->window = 0;

		XCloseDisplay_PTR(nativeLocalStorageContainer->display);
		nativeLocalStorageContainer->display = 0;

		return EGL_FALSE;
	}
  
	logglxcall("glXCreateContext");
	nativeLocalStorageContainer->ctx = glXCreateContext_PTR(nativeLocalStorageContainer->display, visualInfo, NULL, True);

	if (!nativeLocalStorageContainer->ctx)
	{
		nativeLocalStorageContainer->window = 0;

		XCloseDisplay_PTR(nativeLocalStorageContainer->display);
		nativeLocalStorageContainer->display = 0;

		return EGL_FALSE;
	}

	logglxcall("glXMakeCurrent");
	if (!glXMakeCurrent_PTR(nativeLocalStorageContainer->display, nativeLocalStorageContainer->window, nativeLocalStorageContainer->ctx))
	{
		glXDestroyContext_PTR(nativeLocalStorageContainer->display, nativeLocalStorageContainer->ctx);
		nativeLocalStorageContainer->ctx = 0;

		nativeLocalStorageContainer->window = 0;

		XCloseDisplay_PTR(nativeLocalStorageContainer->display);
		nativeLocalStorageContainer->display = 0;

		return EGL_FALSE;
	}

  glXCreateContextAttribsARB_PTR =
    (__PFN_glXCreateContextAttribsARB)
        __getProcAddress("glXCreateContextAttribsARB");
  glXSwapIntervalEXT_PTR =
    (__PFN_glXSwapIntervalEXT)__getProcAddress("glXSwapIntervalEXT");
  glFinish_PTR = (__PFN_glFinish)__getProcAddress("glFinish");
  glFenceSync_PTR = (__PFN_glFenceSync)__getProcAddress("glFenceSync");
  glDeleteSync_PTR = (__PFN_glDeleteSync)__getProcAddress("glDeleteSync");
  glClientWaitSync_PTR = (__PFN_glClientWaitSync)__getProcAddress("glClientWaitSync");
  glWaitSync_PTR = (__PFN_glWaitSync)__getProcAddress("glWaitSync");
  glGetSynciv_PTR = (__PFN_glGetSynciv)__getProcAddress("glGetSynciv");

  int count;
  GLXFBConfig config = NULL;
  {
	  GLXFBConfig* configs = glXGetFBConfigs_PTR(nativeLocalStorageContainer->display, 0, &count);
	  config = configs[0];
	  XFree_PTR(configs);
  }

  EGLint attrib_list[CONTEXT_ATTRIB_LIST_SIZE] = {
		  GLX_CONTEXT_MAJOR_VERSION_ARB, 1,
		  GLX_CONTEXT_MINOR_VERSION_ARB, 0,
		  GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
		  0
  };

  GLXContext testctx;
  EGLint GL_major = 4, GL_minor = 6;
  for (; GL_major >= 1 && !testctx; --GL_major)
  {
	  for (; GL_minor >= 0 && !testctx; --GL_minor)
	  {
		  attrib_list[1] = GL_major;
		  attrib_list[3] = GL_minor;
		  testctx = glXCreateContextAttribsARB_PTR(nativeLocalStorageContainer->display, config, NULL, True, attrib_list);
	  }
  }
  ++GL_major;
  ++GL_minor;

  if (testctx)
  {
	  glXDestroyContext_PTR(nativeLocalStorageContainer->display, testctx);
	  testctx = NULL;
  }
  else
  {
	  GL_major = 0;
	  GL_minor = 0;
  }
  GL_max_supported[0] = GL_major;
  GL_max_supported[1] = GL_minor;


  attrib_list[5] = GLX_CONTEXT_ES_PROFILE_BIT_EXT;
  EGLint ES_major = 3, ES_minor = 2;
  for (; ES_major >= 1 && !testctx; --ES_major)
  {
	  for (; ES_minor >= 0 && !testctx; --ES_minor)
	  {
		  attrib_list[1] = ES_major;
		  attrib_list[3] = ES_minor;
		  testctx = glXCreateContextAttribsARB_PTR(nativeLocalStorageContainer->display, config, NULL, True, attrib_list);
	  }
  }
  ++ES_major;
  ++ES_minor;

  if (testctx)
  {
	  glXDestroyContext_PTR(nativeLocalStorageContainer->display, testctx);
	  testctx = NULL;
  }
  else
  {
	  ES_major = 0;
	  ES_minor = 0;
  }
  ES_max_supported[0] = ES_major;
  ES_max_supported[1] = ES_minor;

  return EGL_TRUE;
}

EGLBoolean __internalTerminate(NativeLocalStorageContainer* nativeLocalStorageContainer)
{
	if (!nativeLocalStorageContainer)
	{
		return EGL_FALSE;
	}

	if (nativeLocalStorageContainer->display)
	{
		logglxcall("glXMakeContextCurrent");
		glXMakeContextCurrent_PTR(nativeLocalStorageContainer->display, 0, 0, 0);
	}

	if (nativeLocalStorageContainer->display && nativeLocalStorageContainer->ctx)
	{
		logglxcall("glXDestroyContext");
		glXDestroyContext_PTR(nativeLocalStorageContainer->display, nativeLocalStorageContainer->ctx);
		nativeLocalStorageContainer->ctx = 0;
	}

	if (nativeLocalStorageContainer->display && nativeLocalStorageContainer->window)
	{
		XDestroyWindow_PTR(nativeLocalStorageContainer->display, nativeLocalStorageContainer->window);
		nativeLocalStorageContainer->window = 0;
	}

	if (nativeLocalStorageContainer->display)
	{
		XCloseDisplay_PTR(nativeLocalStorageContainer->display);
		nativeLocalStorageContainer->display = 0;
	}

	dlclose(libx11);
	dlclose(libgl);

	return EGL_TRUE;
}

EGLBoolean __deleteContext(const EGLDisplayImpl* walkerDpy, const NativeContextContainer* nativeContextContainer)
{
	if (!walkerDpy || !nativeContextContainer)
	{
		return EGL_FALSE;
	}

	logglxcall("glXDestroyContext");
	glXDestroyContext_PTR(walkerDpy->display_id, nativeContextContainer->ctx);

	return EGL_TRUE;
}

EGLBoolean __processAttribList(EGLenum api, EGLint* target_attrib_list, const EGLint* attrib_list, EGLint* error)
{
	if (!target_attrib_list || !attrib_list || !error)
	{
		return EGL_FALSE;
	}

	const EGLint defaultProfileMask = ((api == EGL_OPENGL_ES_API) ? GLX_CONTEXT_ES_PROFILE_BIT_EXT : GLX_CONTEXT_CORE_PROFILE_BIT_ARB);
	EGLint template_attrib_list[CONTEXT_ATTRIB_LIST_SIZE] = {
			GLX_CONTEXT_MAJOR_VERSION_ARB, 1,
			GLX_CONTEXT_MINOR_VERSION_ARB, 0,
			GLX_CONTEXT_FLAGS_ARB, 0,
			GLX_CONTEXT_PROFILE_MASK_ARB, defaultProfileMask,
			GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB, GLX_NO_RESET_NOTIFICATION_ARB,
			0
	};
	template_attrib_list[CONTEXT_ATTRIB_LIST_SIZE-1] = 0;

	EGLint attribListIndex = 0;

	while (attrib_list[attribListIndex] != EGL_NONE)
	{
		EGLint value = attrib_list[attribListIndex + 1];

		switch (attrib_list[attribListIndex])
		{
			case EGL_CONTEXT_MAJOR_VERSION:
			{
				if (value < 1)
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}

				template_attrib_list[1] = value;
			}
			break;
			case EGL_CONTEXT_MINOR_VERSION:
			{
				if (value < 0)
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}

				template_attrib_list[3] = value;
			}
			break;
			case EGL_CONTEXT_OPENGL_PROFILE_MASK:
			{
				if (value == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT)
				{
					template_attrib_list[7] = GLX_CONTEXT_CORE_PROFILE_BIT_ARB;
				}
				else if (value == EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT)
				{
					template_attrib_list[7] = GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
				}
				else
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}
			}
			break;
			case EGL_CONTEXT_OPENGL_DEBUG:
			{
				if (value == EGL_TRUE)
				{
					template_attrib_list[5] |= GLX_CONTEXT_DEBUG_BIT_ARB;
				}
				else if (value == EGL_FALSE)
				{
					template_attrib_list[5] &= ~GLX_CONTEXT_DEBUG_BIT_ARB;
				}
				else
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}
			}
			break;
			case EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE:
			{
				if (value == EGL_TRUE)
				{
					template_attrib_list[5] |= GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
				}
				else if (value == EGL_FALSE)
				{
					template_attrib_list[5] &= ~GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
				}
				else
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}
			}
			break;
			case EGL_CONTEXT_OPENGL_ROBUST_ACCESS:
			{
				if (value == EGL_TRUE)
				{
					template_attrib_list[5] |= GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB;
				}
				else if (value == EGL_FALSE)
				{
					template_attrib_list[5] &= ~GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB;
				}
				else
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}
			}
			break;
			case EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY:
			{
				if (value == EGL_NO_RESET_NOTIFICATION)
				{
					template_attrib_list[9] = GLX_NO_RESET_NOTIFICATION_ARB;
				}
				else if (value == EGL_LOSE_CONTEXT_ON_RESET)
				{
					template_attrib_list[9] = GLX_LOSE_CONTEXT_ON_RESET_ARB;
				}
				else
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}
			}
			break;
			default:
			{
				*error = EGL_BAD_ATTRIBUTE;

				return EGL_FALSE;
			}
			break;
		}

		attribListIndex += 2;

		// More than 14 entries can not exist.
		if (attribListIndex >= 7 * 2)
		{
			*error = EGL_BAD_ATTRIBUTE;

			return EGL_FALSE;
		}
	}

	memcpy(target_attrib_list, template_attrib_list, CONTEXT_ATTRIB_LIST_SIZE * sizeof(EGLint));

	return EGL_TRUE;
}

EGLBoolean __createPbufferSurface(EGLSurfaceImpl* newSurface, const EGLint* attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error)
{
	if (!newSurface || !walkerDpy || !walkerConfig || !error)
	{
		return EGL_FALSE;
	}
	if (!walkerConfig->drawToPBuffer)
	{
		return EGL_FALSE;
	}

	EGLNativeDisplayType display = walkerDpy->display_id;

	int glxattribs[] =
	{
		GLX_PBUFFER_WIDTH, 0,
		GLX_PBUFFER_HEIGHT, 0,
		GLX_LARGEST_PBUFFER, False,

		None
	};
	int* width = glxattribs + 1;
	int* height = glxattribs + 3;
	int* largest_pbuffer = glxattribs + 5;
	EGLBoolean colorspace_srgb = 0;  // default: linear per spec
	EGLBoolean mipmapTexture = EGL_FALSE;
	EGLint textureFormat = EGL_NO_TEXTURE;
	EGLint textureTarget = EGL_NO_TEXTURE;

	EGLint currAttrib = 0;
	while (attrib_list[currAttrib] != EGL_NONE)
	{
		EGLint attrib = attrib_list[currAttrib];
		EGLint value = attrib_list[currAttrib + 1];

		switch (attrib)
		{
		case EGL_WIDTH:
			*width = value; break;
		case EGL_HEIGHT:
			*height = value; break;
		case EGL_LARGEST_PBUFFER:
			*largest_pbuffer = value; break;
		case EGL_GL_COLORSPACE:
			if (value == EGL_GL_COLORSPACE_LINEAR)
				colorspace_srgb = 0;
			else if (value == EGL_GL_COLORSPACE_SRGB)
				// Only request sRGB if the driver supports it; otherwise silently
				// fall back to linear per spec.
				colorspace_srgb = walkerDpy->srgbFramebufferSupported ? 1 : 0;
			else
			{
				*error = EGL_BAD_ATTRIBUTE;
				return EGL_FALSE;
			}
			break;
		case EGL_MIPMAP_TEXTURE:
			mipmapTexture = (EGLBoolean)value; break;
		case EGL_TEXTURE_FORMAT:
			if (value != EGL_NO_TEXTURE && value != EGL_TEXTURE_RGB && value != EGL_TEXTURE_RGBA)
			{
				*error = EGL_BAD_ATTRIBUTE;
				return EGL_FALSE;
			}
			textureFormat = value; break;
		case EGL_TEXTURE_TARGET:
			if (value != EGL_NO_TEXTURE && value != EGL_TEXTURE_2D)
			{
				*error = EGL_BAD_ATTRIBUTE;
				return EGL_FALSE;
			}
			textureTarget = value; break;
		case EGL_VG_ALPHA_FORMAT:
		case EGL_VG_COLORSPACE:
			*error = EGL_BAD_MATCH;
			return EGL_FALSE;
		}

		currAttrib += 2;
	}

	GLXFBConfig config = 0;
	int glxchooseAttribs[] = {
		GLX_BUFFER_SIZE, walkerConfig->bufferSize,
		GLX_LEVEL, walkerConfig->level,
		GLX_DOUBLEBUFFER, walkerConfig->doubleBuffer,
		GLX_RED_SIZE, walkerConfig->redSize,
		GLX_GREEN_SIZE, walkerConfig->greenSize,
		GLX_BLUE_SIZE, walkerConfig->blueSize,
		GLX_ALPHA_SIZE, walkerConfig->alphaSize,
		GLX_DEPTH_SIZE, walkerConfig->depthSize,
		GLX_STENCIL_SIZE, walkerConfig->stencilSize,
		GLX_RENDER_TYPE, GLX_RGBA_BIT,
		//GLX_DRAWABLE_TYPE, (walkerConfig->drawToWindow ?  GLX_WINDOW_BIT : 0) | (walkerConfig->drawToPBuffer ? GLX_PBUFFER_BIT : 0) | (walkerConfig->drawToPixmap ?  GLX_PIXMAP_BIT : 0),
		GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
		GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB, colorspace_srgb,
		GLX_X_RENDERABLE, True,
		None
	};

	EGLint numConfigs = 0;
	GLXFBConfig* chooseRetval = glXChooseFBConfig_PTR(walkerDpy->display_id, DefaultScreen(walkerDpy->display_id), glxchooseAttribs, &numConfigs);
	if (chooseRetval==NULL || numConfigs==0)
		return EGL_FALSE;
	config = chooseRetval[0];
	XFree_PTR(chooseRetval);

	newSurface->drawToWindow = EGL_FALSE;
	newSurface->drawToPixmap = EGL_FALSE;
	newSurface->drawToPBuffer = EGL_TRUE;
	newSurface->doubleBuffer = EGL_FALSE;
	newSurface->configId = walkerConfig->configId;
	newSurface->width = *width;
	newSurface->height = *height;
	newSurface->swapBehavior = EGL_BUFFER_DESTROYED;
	newSurface->multisampleResolve = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
	newSurface->mipmapLevel = 0;
	newSurface->mipmapTexture = mipmapTexture;
	newSurface->largestPbuffer = (EGLBoolean)*largest_pbuffer;
	newSurface->textureFormat = textureFormat;
	newSurface->textureTarget = textureTarget;
	newSurface->glColorspace = colorspace_srgb ? EGL_GL_COLORSPACE_SRGB : EGL_GL_COLORSPACE_LINEAR;

	newSurface->initialized = EGL_TRUE;
	newSurface->destroy = EGL_FALSE;
	logglxcall("glXCreatePbuffer");
	newSurface->pbuf = glXCreatePbuffer_PTR(display, config, glxattribs);
	newSurface->nativeSurfaceContainer.config = config;
	newSurface->nativeSurfaceContainer.drawable = newSurface->pbuf;

	return EGL_TRUE;
}

EGLBoolean __createWindowSurface(EGLSurfaceImpl* newSurface, EGLNativeWindowType win, const EGLint *attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error)
{
	if (!newSurface || !walkerDpy || !walkerConfig || !error)
	{
		return EGL_FALSE;
	}

	EGLBoolean colorspace_srgb = 0;
	EGLBoolean doublebuffer = 0;
	if (attrib_list)
	{
		EGLint indexAttribList = 0;

		while (attrib_list[indexAttribList] != EGL_NONE)
		{
			EGLint value = attrib_list[indexAttribList + 1];

			switch (attrib_list[indexAttribList])
			{
				case EGL_GL_COLORSPACE:
				{
					if (value == EGL_GL_COLORSPACE_LINEAR)
					{
						colorspace_srgb = 0;
					}
					else if (value == EGL_GL_COLORSPACE_SRGB)
					{
						// Only request sRGB if the driver supports it; otherwise
						// silently fall back to linear per spec.
						colorspace_srgb = walkerDpy->srgbFramebufferSupported ? 1 : 0;
					}
					else
					{
						*error = EGL_BAD_ATTRIBUTE;

						return EGL_FALSE;
					}
				}
				break;
				case EGL_RENDER_BUFFER:
				{
					if (value == EGL_SINGLE_BUFFER)
					{
						doublebuffer = 0;
						if (walkerConfig->doubleBuffer)
						{
							*error = EGL_BAD_MATCH;

							return EGL_FALSE;
						}
					}
					else if (value == EGL_BACK_BUFFER)
					{
						doublebuffer = 1;
						if (!walkerConfig->doubleBuffer)
						{
							*error = EGL_BAD_MATCH;

							return EGL_FALSE;
						}
					}
					else
					{
						*error = EGL_BAD_ATTRIBUTE;

						return EGL_FALSE;
					}
				}
				break;
				case EGL_VG_ALPHA_FORMAT:
				{
					*error = EGL_BAD_MATCH;

					return EGL_FALSE;
				}
				break;
				case EGL_VG_COLORSPACE:
				{
					*error = EGL_BAD_MATCH;

					return EGL_FALSE;
				}
				break;
			}

			indexAttribList += 2;

			// More than 4 entries can not exist.
			if (indexAttribList >= 4 * 2)
			{
				*error = EGL_BAD_ATTRIBUTE;

				return EGL_FALSE;
			}
		}
	}

	//

	GLXFBConfig config = 0;
	int glxchooseAttribs[] = {
		GLX_BUFFER_SIZE, walkerConfig->bufferSize,
		GLX_LEVEL, walkerConfig->level,
		GLX_DOUBLEBUFFER, doublebuffer,
		GLX_RED_SIZE, walkerConfig->redSize,
		GLX_GREEN_SIZE, walkerConfig->greenSize,
		GLX_BLUE_SIZE, walkerConfig->blueSize,
		GLX_ALPHA_SIZE, walkerConfig->alphaSize,
		GLX_DEPTH_SIZE, walkerConfig->depthSize,
		GLX_STENCIL_SIZE, walkerConfig->stencilSize,
		GLX_RENDER_TYPE, GLX_RGBA_BIT,
		//GLX_DRAWABLE_TYPE, (walkerConfig->drawToWindow ?  GLX_WINDOW_BIT : 0) | (walkerConfig->drawToPBuffer ? GLX_PBUFFER_BIT : 0) | (walkerConfig->drawToPixmap ?  GLX_PIXMAP_BIT : 0),
		GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
		GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB, colorspace_srgb,
		GLX_X_RENDERABLE, True,
		None
	};

	EGLint numConfigs = 0;
	GLXFBConfig* chooseRetval = glXChooseFBConfig_PTR(walkerDpy->display_id, DefaultScreen(walkerDpy->display_id), glxchooseAttribs, &numConfigs);
	if (chooseRetval==NULL || numConfigs==0)
		return EGL_FALSE;
	config = chooseRetval[0];
	XFree_PTR(chooseRetval);

	newSurface->drawToWindow = EGL_TRUE;
	newSurface->drawToPixmap = EGL_FALSE;
	newSurface->drawToPBuffer = EGL_FALSE;
	newSurface->doubleBuffer = walkerConfig->doubleBuffer;
	newSurface->configId = walkerConfig->configId;

	XWindowAttributes xwa;
	XGetWindowAttributes(walkerDpy->display_id, win, &xwa);
	newSurface->width = xwa.width;
	newSurface->height = xwa.height;
	newSurface->swapBehavior = EGL_BUFFER_DESTROYED;
	newSurface->multisampleResolve = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
	newSurface->mipmapLevel = 0;
	newSurface->mipmapTexture = EGL_FALSE;
	newSurface->largestPbuffer = EGL_FALSE;
	newSurface->textureFormat = EGL_NO_TEXTURE;
	newSurface->textureTarget = EGL_NO_TEXTURE;
	newSurface->glColorspace = colorspace_srgb ? EGL_GL_COLORSPACE_SRGB : EGL_GL_COLORSPACE_LINEAR;

	newSurface->initialized = EGL_TRUE;
	newSurface->destroy = EGL_FALSE;
	newSurface->win = win;
	newSurface->nativeSurfaceContainer.config = config;
	newSurface->nativeSurfaceContainer.drawable = win;

	return EGL_TRUE;
}

EGLBoolean __destroySurface(EGLNativeDisplayType dpy, const EGLSurfaceImpl* surface)
{
	if (!surface)
	{
		return EGL_FALSE;
	}

	if (surface->drawToPBuffer)
	{
		logglxcall("glXDestroyPbuffer");
		glXDestroyPbuffer_PTR(dpy, surface->pbuf);
	}
	else if (surface->drawToPixmap)
	{
		if (glXDestroyPixmap_PTR)
			glXDestroyPixmap_PTR(dpy, surface->nativeSurfaceContainer.drawable);
	}
	// else Nothing to release.

	return EGL_TRUE;
}

EGLBoolean __createPixmapSurface(EGLSurfaceImpl* newSurface, EGLNativePixmapType pixmap,
	const EGLint *attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error)
{
	(void)attrib_list;
	if (!newSurface || !walkerDpy || !walkerConfig || !error)
		return EGL_FALSE;

	if (!pixmap)
	{
		*error = EGL_BAD_NATIVE_PIXMAP;
		return EGL_FALSE;
	}

	EGLint glColorspace = EGL_GL_COLORSPACE_LINEAR;
	if (attrib_list)
	{
		EGLint i = 0;
		while (attrib_list[i] != EGL_NONE)
		{
			EGLint attrib = attrib_list[i];
			EGLint value  = attrib_list[i + 1];
			switch (attrib)
			{
				case EGL_GL_COLORSPACE:
					if (value == EGL_GL_COLORSPACE_LINEAR || value == EGL_GL_COLORSPACE_SRGB)
						glColorspace = value;
					else
					{
						*error = EGL_BAD_ATTRIBUTE;
						return EGL_FALSE;
					}
					break;
				case EGL_VG_ALPHA_FORMAT:
				case EGL_VG_COLORSPACE:
					*error = EGL_BAD_MATCH;
					return EGL_FALSE;
				default:
					*error = EGL_BAD_ATTRIBUTE;
					return EGL_FALSE;
			}
			i += 2;
		}
	}

	if (!glXCreatePixmap_PTR)
	{
		*error = EGL_BAD_MATCH;
		return EGL_FALSE;
	}

	EGLNativeDisplayType display = walkerDpy->display_id;

	int glxattribs[] = {
		GLX_BUFFER_SIZE, walkerConfig->bufferSize,
		GLX_RED_SIZE,    walkerConfig->redSize,
		GLX_GREEN_SIZE,  walkerConfig->greenSize,
		GLX_BLUE_SIZE,   walkerConfig->blueSize,
		GLX_ALPHA_SIZE,  walkerConfig->alphaSize,
		GLX_DEPTH_SIZE,  walkerConfig->depthSize,
		GLX_STENCIL_SIZE,walkerConfig->stencilSize,
		GLX_RENDER_TYPE, GLX_RGBA_BIT,
		GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
		None
	};

	EGLint numConfigs = 0;
	GLXFBConfig* configs = glXChooseFBConfig_PTR(display, DefaultScreen(display), glxattribs, &numConfigs);
	if (!configs || numConfigs == 0)
	{
		*error = EGL_BAD_MATCH;
		return EGL_FALSE;
	}
	GLXFBConfig fbconfig = configs[0];
	XFree_PTR(configs);

	const int pixmap_attribs[] = { None };
	logglxcall("glXCreatePixmap");
	GLXPixmap glxpixmap = glXCreatePixmap_PTR(display, fbconfig, pixmap, pixmap_attribs);
	if (!glxpixmap)
	{
		*error = EGL_BAD_NATIVE_PIXMAP;
		return EGL_FALSE;
	}

	// Query pixmap geometry for width/height.
	Window root;
	int x, y;
	unsigned int w = 0, h = 0, border, depth;
	if (XGetGeometry_PTR)
		XGetGeometry_PTR(display, pixmap, &root, &x, &y, &w, &h, &border, &depth);

	newSurface->drawToWindow = EGL_FALSE;
	newSurface->drawToPixmap = EGL_TRUE;
	newSurface->drawToPBuffer = EGL_FALSE;
	newSurface->doubleBuffer = EGL_FALSE;
	newSurface->configId = walkerConfig->configId;
	newSurface->width  = (EGLint)w;
	newSurface->height = (EGLint)h;
	newSurface->swapBehavior = EGL_BUFFER_DESTROYED;
	newSurface->multisampleResolve = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
	newSurface->mipmapLevel = 0;
	newSurface->mipmapTexture = EGL_FALSE;
	newSurface->largestPbuffer = EGL_FALSE;
	newSurface->textureFormat = EGL_NO_TEXTURE;
	newSurface->textureTarget = EGL_NO_TEXTURE;
	newSurface->glColorspace = glColorspace;

	newSurface->initialized = EGL_TRUE;
	newSurface->destroy = EGL_FALSE;
	newSurface->pixmap = pixmap;
	newSurface->nativeSurfaceContainer.drawable = glxpixmap;
	newSurface->nativeSurfaceContainer.config = fbconfig;

	return EGL_TRUE;
}

EGLBoolean __copyBuffers(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* surface, EGLNativePixmapType target)
{
	(void)surface;

	if (!target || !walkerDpy)
		return EGL_FALSE;

	Display* display = walkerDpy->display_id;

	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	GLint width  = viewport[2];
	GLint height = viewport[3];

	if (width <= 0 || height <= 0)
		return EGL_FALSE;

	size_t rowBytes = (size_t)width * 4;
	GLubyte* pixels = (GLubyte*)malloc(rowBytes * (size_t)height);
	if (!pixels)
		return EGL_FALSE;

	// GL_BGRA = 0x80E1; read bottom-up
	glReadPixels(0, 0, width, height, 0x80E1, GL_UNSIGNED_BYTE, pixels);

	// Flip rows: GL origin is bottom-left, X origin is top-left.
	GLubyte* flipped = (GLubyte*)malloc(rowBytes * (size_t)height);
	if (!flipped) { free(pixels); return EGL_FALSE; }
	for (GLint row = 0; row < height; row++)
		memcpy(flipped + (size_t)row * rowBytes, pixels + (size_t)(height - 1 - row) * rowBytes, rowBytes);
	free(pixels);

	if (!XDefaultVisual_PTR || !XDefaultDepth_PTR || !XCreateImage_PTR || !XPutImage_PTR || !XCreateGC_PTR || !XFreeGC_PTR)
	{
		free(flipped);
		return EGL_FALSE;
	}

	int screen  = DefaultScreen(display);
	Visual* vis = XDefaultVisual_PTR(display, screen);
	int depth   = XDefaultDepth_PTR(display, screen);

	XImage* ximage = XCreateImage_PTR(display, vis, (unsigned int)depth, ZPixmap, 0,
		(char*)flipped, (unsigned int)width, (unsigned int)height, 32, (int)rowBytes);
	if (!ximage) { free(flipped); return EGL_FALSE; }

	GC gc = XCreateGC_PTR(display, target, 0, NULL);
	XPutImage_PTR(display, target, gc, ximage, 0, 0, 0, 0, (unsigned int)width, (unsigned int)height);
	XFreeGC_PTR(display, gc);

	// Prevent XDestroyImage from freeing our data; free both manually.
	ximage->data = NULL;
	XFree_PTR(ximage);
	free(flipped);

	return EGL_TRUE;
}

EGLBoolean __initialize(EGLDisplayImpl* walkerDpy, const NativeLocalStorageContainer* nativeLocalStorageContainer, EGLint* error)
{
	if (!walkerDpy || !nativeLocalStorageContainer || !error)
	{
		return EGL_FALSE;
	}

	logglxcall("glXQueryExtensionsString");
	const char* extensions_str = glXQueryExtensionsString_PTR(walkerDpy->display_id, DefaultScreen(walkerDpy->display_id));
	int ES_supported = strstr(extensions_str, "GLX_EXT_create_context_es_profile") != NULL;
	const EGLint ES_mask = ES_supported * (EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT);

	walkerDpy->srgbFramebufferSupported =
		(strstr(extensions_str, "GLX_ARB_framebuffer_sRGB") != NULL ||
		 strstr(extensions_str, "GLX_EXT_framebuffer_sRGB") != NULL) ? EGL_TRUE : EGL_FALSE;

	// Create configuration list.

	EGLint numberPixelFormats;

	logglxcall("glXGetFBConfigs");
	GLXFBConfig* fbConfigs = glXGetFBConfigs_PTR(walkerDpy->display_id, DefaultScreen(walkerDpy->display_id), &numberPixelFormats);

	if (!fbConfigs || numberPixelFormats == 0)
	{
		if (fbConfigs)
		{
			XFree_PTR(fbConfigs);
		}

		*error = EGL_NOT_INITIALIZED;

		return EGL_FALSE;
	}

	EGLint attribute;

	XVisualInfo* visualInfo;

	EGLConfigImpl* lastConfig = 0;
	for (EGLint currentPixelFormat = 0; currentPixelFormat < numberPixelFormats; currentPixelFormat++)
	{
		EGLint value;

		attribute = GLX_VISUAL_ID;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &value))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		if (!value)
		{
			continue;
		}

		// No check for OpenGL.

		attribute = GLX_RENDER_TYPE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &value))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		if (!(value & GLX_RGBA_BIT))
		{
			continue;
		}

		attribute = GLX_TRANSPARENT_TYPE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &value))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		if (value == GLX_TRANSPARENT_INDEX)
		{
			continue;
		}

		//

		EGLConfigImpl* newConfig = (EGLConfigImpl*)malloc(sizeof(EGLConfigImpl));
		if (!newConfig)
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		_eglInternalSetDefaultConfig(newConfig);

		// Store in the same order as received.
		newConfig->next = 0;
		if (lastConfig != 0)
		{
			lastConfig->next = newConfig;
		}
		else
		{
			walkerDpy->rootConfig = newConfig;
		}
		lastConfig = newConfig;

		//

		attribute = GLX_DRAWABLE_TYPE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &value))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		newConfig->drawToWindow = value & GLX_WINDOW_BIT ? EGL_TRUE : EGL_FALSE;
		newConfig->drawToPixmap = value & GLX_PIXMAP_BIT ? EGL_TRUE : EGL_FALSE;
		newConfig->drawToPBuffer = value & GLX_PBUFFER_BIT ? EGL_TRUE : EGL_FALSE;

		attribute = GLX_DOUBLEBUFFER;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->doubleBuffer))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		//
		newConfig->conformant = (EGL_OPENGL_BIT | ES_mask);
		newConfig->renderableType = (EGL_OPENGL_BIT | ES_mask);
		newConfig->surfaceType = 0;
		if (newConfig->drawToWindow)
		{
			newConfig->surfaceType |= EGL_WINDOW_BIT;
		}
		if (newConfig->drawToPixmap)
		{
			newConfig->surfaceType |= EGL_PIXMAP_BIT;
		}
		if (newConfig->drawToPBuffer)
		{
			newConfig->surfaceType |= EGL_PBUFFER_BIT;
		}
		newConfig->colorBufferType = EGL_RGB_BUFFER;
		newConfig->configId = currentPixelFormat;

		attribute = GLX_BUFFER_SIZE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->bufferSize))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = GLX_RED_SIZE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->redSize))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = GLX_GREEN_SIZE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->greenSize))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = GLX_BLUE_SIZE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->blueSize))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = GLX_ALPHA_SIZE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->alphaSize))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = GLX_DEPTH_SIZE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->depthSize))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = GLX_STENCIL_SIZE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->stencilSize))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}


		//

		attribute = GLX_SAMPLE_BUFFERS;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->sampleBuffers))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = GLX_SAMPLES;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->samples))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		//

		attribute = GLX_BIND_TO_TEXTURE_RGB_EXT;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->bindToTextureRGB))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		newConfig->bindToTextureRGB = newConfig->bindToTextureRGB ? EGL_TRUE : EGL_FALSE;

		attribute = GLX_BIND_TO_TEXTURE_RGBA_EXT;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->bindToTextureRGBA))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		newConfig->bindToTextureRGBA = newConfig->bindToTextureRGBA ? EGL_TRUE : EGL_FALSE;

		//

		attribute = GLX_MAX_PBUFFER_PIXELS;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->maxPBufferPixels))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = GLX_MAX_PBUFFER_WIDTH;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->maxPBufferWidth))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = GLX_MAX_PBUFFER_HEIGHT;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->maxPBufferHeight))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		//

		attribute = GLX_TRANSPARENT_TYPE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->transparentType))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		newConfig->transparentType = newConfig->transparentType == GLX_TRANSPARENT_RGB ? EGL_TRANSPARENT_RGB : EGL_NONE;

		attribute = GLX_TRANSPARENT_RED_VALUE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->transparentRedValue))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = GLX_TRANSPARENT_GREEN_VALUE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->transparentGreenValue))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = GLX_TRANSPARENT_BLUE_VALUE;
		logglxcall("glXGetFBConfigAttrib");
		if (glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->transparentBlueValue))
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		//
		logglxcall("glXGetVisualFromFBConfig");
		visualInfo = glXGetVisualFromFBConfig_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat]);

		if (!visualInfo)
		{
			XFree_PTR(fbConfigs);

			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		newConfig->nativeVisualId = visualInfo->visualid;

		XFree_PTR(visualInfo);

		newConfig->matchNativePixmap = EGL_NONE;
		newConfig->nativeRenderable = EGL_DONT_CARE; // ???

		// Query configCaveat from GLX_CONFIG_CAVEAT.
		attribute = GLX_CONFIG_CAVEAT;
		if (!glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &value))
		{
			if (value == GLX_SLOW_CONFIG)
				newConfig->configCaveat = EGL_SLOW_CONFIG;
			else if (value == GLX_NON_CONFORMANT_CONFIG)
				newConfig->configCaveat = EGL_NON_CONFORMANT_CONFIG;
			else
				newConfig->configCaveat = EGL_NONE;
		}
		else
		{
			newConfig->configCaveat = EGL_NONE;
		}

		// Query level.
		attribute = GLX_LEVEL;
		if (!glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &value))
			newConfig->level = value;
		else
			newConfig->level = 0;

		// Query nativeVisualType.
		attribute = GLX_X_VISUAL_TYPE;
		if (!glXGetFBConfigAttrib_PTR(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &value))
			newConfig->nativeVisualType = value;
		else
			newConfig->nativeVisualType = EGL_NONE;

		// GLX has no direct equivalent for swap interval range; use desktop defaults.
		newConfig->minSwapInterval = 0;
		newConfig->maxSwapInterval = 1;
	}

	XFree_PTR(fbConfigs);

	return EGL_TRUE;
}

static int xerrorhandler(Display *dsp, XErrorEvent *error)
{
  char errorstring[1024];
  XGetErrorText_PTR(dsp, error->error_code, errorstring, 1024);
  int minor = error->minor_code;
  int err = error->error_code;
    
  std::cerr << "X error--" << errorstring << " minor = " << minor << " major = " << err << std::endl;
  exit(-1);
}
EGLBoolean __createContext(NativeContextContainer* nativeContextContainer, const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* sharedNativeContextContainer, const EGLint* attribList)
{
	if (!nativeContextContainer || !walkerDpy || !nativeSurfaceContainer)
	{
		return EGL_FALSE;
	}
	//XSetErrorHandler(xerrorhandler);
	logglxcall("glXCreateContextAttribsARB");
	nativeContextContainer->ctx = glXCreateContextAttribsARB_PTR(walkerDpy->display_id, nativeSurfaceContainer->config, sharedNativeContextContainer ? sharedNativeContextContainer->ctx : 0, True, attribList);

	return nativeContextContainer->ctx != 0;
}

EGLBoolean __makeCurrent(const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* nativeContextContainer)
{
	if (!walkerDpy || (!nativeSurfaceContainer && nativeContextContainer) || (nativeSurfaceContainer && !nativeContextContainer))
	{
		return EGL_FALSE;
	}

	if (!nativeContextContainer)
	{
		logglxcall("glXMakeCurrent");
		return (EGLBoolean)glXMakeCurrent_PTR(walkerDpy->display_id, None, NULL);
	}

	logglxcall("glXMakeCurrent");
	return (EGLBoolean)glXMakeCurrent_PTR(walkerDpy->display_id, nativeSurfaceContainer->drawable, nativeContextContainer->ctx);
}

EGLBoolean __swapBuffers(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface)
{
	if (!walkerDpy || !walkerSurface)
	{
		return EGL_FALSE;
	}

	logglxcall("glXSwapBuffers");
	glXSwapBuffers_PTR(walkerDpy->display_id, walkerSurface->win);

	return EGL_TRUE;
}

EGLBoolean __swapInterval(const EGLDisplayImpl* walkerDpy, EGLint interval)
{
	if (!walkerDpy)
	{
		return EGL_FALSE;
	}

	logglxcall("glXSwapIntervalEXT");
	glXSwapIntervalEXT_PTR(walkerDpy->display_id, walkerDpy->currentDraw->win, interval);

	return EGL_TRUE;
}

EGLBoolean __bindTexImage(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface, EGLint buffer)
{
	if (!walkerDpy || !walkerSurface)
		return EGL_FALSE;
	if (!glXBindTexImageEXT_PTR)
		return EGL_FALSE;

	// GLX_EXT_texture_from_pixmap buffer values.
	// EGL_BACK_BUFFER maps to GLX_BACK_LEFT_EXT (0x20E0).
	// EGL_FRONT_BUFFER (single-buffered) maps to GLX_FRONT_LEFT_EXT (0x20DE).
	int glxBuffer = (buffer == EGL_BACK_BUFFER) ? 0x20E0 : 0x20DE;
	glXBindTexImageEXT_PTR(walkerDpy->display_id, walkerSurface->pbuf, glxBuffer, NULL);
	return EGL_TRUE;
}

EGLBoolean __releaseTexImage(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface, EGLint buffer)
{
	if (!walkerDpy || !walkerSurface)
		return EGL_FALSE;
	if (!glXReleaseTexImageEXT_PTR)
		return EGL_FALSE;

	int glxBuffer = (buffer == EGL_BACK_BUFFER) ? 0x20E0 : 0x20DE;
	glXReleaseTexImageEXT_PTR(walkerDpy->display_id, walkerSurface->pbuf, glxBuffer);
	return EGL_TRUE;
}

/*
EGLBoolean __getPlatformDependentHandles(void* _out, const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* nativeContextContainer)
{
	if (!nativeSurfaceContainer || !nativeContextContainer)
		return EGL_FALSE;

	EGLContextInternals* out = (EGLContextInternals*) _out;

	out->display = walkerDpy->display_id;
	out->context = nativeContextContainer->ctx;
	out->surface.drawable = nativeSurfaceContainer->drawable;
	out->surface.config = nativeSurfaceContainer->config;

	return EGL_TRUE;
}
*/