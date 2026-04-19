#include "egl_common.h"
#include <cstring>

extern "C"
{

EGLint _eglGetError(void)
{
	EGLint currentError = g_localStorage.error;

	g_localStorage.error = EGL_SUCCESS;

	return currentError;
}

EGLDisplay _eglGetDisplay(EGLNativeDisplayType display_id)
{
	if (!_eglInternalInit())
	{
		return EGL_NO_DISPLAY;
	}

	//
	{
		auto _rl = g_globalStorage.placeRootDpy_readlock();

		EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

		while (walkerDpy)
		{
			if (walkerDpy->display_id == display_id)
			{
				return (EGLDisplay)walkerDpy;
			}

			walkerDpy = walkerDpy->next;
		}
	}

	EGLDisplayImpl* newDpy = new EGLDisplayImpl();

	if (!newDpy)
	{
		return EGL_NO_DISPLAY;
	}

	newDpy->initialized = EGL_FALSE;
	newDpy->destroy = EGL_FALSE;
	{
		auto dummy = g_globalStorage.dummy_read();
		newDpy->display_id = display_id ? display_id : __getDefaultNativeDisplay(&dummy);
	}
	newDpy->rootSurface = 0;
	newDpy->rootCtx = 0;
	newDpy->rootConfig = 0;
	newDpy->rootSync = nullptr;
	newDpy->rootImage = nullptr;
	newDpy->currentDraw = EGL_NO_SURFACE_IMPL;
	newDpy->currentRead = EGL_NO_SURFACE_IMPL;
	newDpy->currentCtx = EGL_NO_CONTEXT_IMPL;
	newDpy->next = g_globalStorage.rootDpy;

	auto _wl = g_globalStorage.placeRootDpy_writelock();
	g_globalStorage.rootDpy = newDpy;

	return newDpy;
}

EGLBoolean _eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
	auto _rl = g_globalStorage.placeRootDpy_readlock();
	EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
			guard_t _{ walkerDpy->mutex };

			if (walkerDpy->destroy)
			{
				g_localStorage.error = EGL_NOT_INITIALIZED;

				return EGL_FALSE;
			}

			{
				auto dummy = g_globalStorage.dummy_read();
				EGLBoolean fail = (!walkerDpy->initialized && !__initialize(walkerDpy, &dummy, &g_localStorage.error));
				g_globalStorage.dummy_write(dummy);
				if (fail)
				{
					return EGL_FALSE;
				}
			}

			walkerDpy->initialized = EGL_TRUE;


			//

			if (major)
			{
				*major = 1;
			}

			if (minor)
			{
				*minor = 5;
			}

			return EGL_TRUE;
		}

		walkerDpy = walkerDpy->next;
	}

	g_localStorage.error = EGL_BAD_DISPLAY;

	return EGL_FALSE;
}

EGLBoolean _eglTerminate(EGLDisplay dpy)
{
	EGLBoolean success = EGL_FALSE;
	{
		auto _rl = g_globalStorage.placeRootDpy_readlock();
		EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

		while (walkerDpy)
		{
			if ((EGLDisplay)walkerDpy == dpy)
			{
				guard_t _{ walkerDpy->mutex };

				if (!walkerDpy->initialized || walkerDpy->destroy)
				{
					return EGL_TRUE;
				}

				walkerDpy->initialized = EGL_FALSE;
				walkerDpy->destroy = EGL_TRUE;

				success = EGL_TRUE;
			}

			walkerDpy = walkerDpy->next;
		}
	}

	if (success)
	{
		_eglInternalCleanup();
		return EGL_TRUE;
	}

	g_localStorage.error = EGL_BAD_DISPLAY;
	return EGL_FALSE;
}

const char *_eglQueryString(EGLDisplay dpy, EGLint name)
{
	if (dpy == EGL_NO_DISPLAY)
	{
		if (name == EGL_EXTENSIONS)
			return "EGL_EXT_client_extensions EGL_EXT_platform_device";
		g_localStorage.error = EGL_BAD_DISPLAY;
		return nullptr;
	}

	auto _rl = g_globalStorage.placeRootDpy_readlock();
	EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
			guard_t _{ walkerDpy->mutex };

			if (!walkerDpy->initialized || walkerDpy->destroy)
			{
				g_localStorage.error = EGL_NOT_INITIALIZED;

				return 0;
			}

			switch (name)
			{
				case EGL_CLIENT_APIS:
				{
					bool glOK = (g_GL_max_supported_version[0] > 0);
					bool esOK = (g_ES_max_supported_version[0] > 0);
					if (glOK && esOK) return "OpenGL OpenGL_ES";
					if (glOK)         return "OpenGL";
					if (esOK)         return "OpenGL_ES";
					return "";
				}
				break;
				case EGL_VENDOR:
				{
					return _EGL_VENDOR;
				}
				break;
				case EGL_VERSION:
				{
					return _EGL_VERSION;
				}
				break;
				case EGL_EXTENSIONS:
				{
					static thread_local char extBuf[2048];
					extBuf[0] = '\0';
					uint32_t hdr = walkerDpy->supportedHDRColorspaces;
					auto appendExt = [&](const char* s) {
						if (extBuf[0]) strncat(extBuf, " ", sizeof(extBuf) - strlen(extBuf) - 1);
						strncat(extBuf, s, sizeof(extBuf) - strlen(extBuf) - 1);
					};
					appendExt("EGL_KHR_gl_colorspace");
					appendExt("EGL_KHR_create_context");
					appendExt("EGL_EXT_client_extensions");
					if (hdr & EGL_HDR_CS_SCRGB_LINEAR_BIT)  appendExt("EGL_EXT_gl_colorspace_scrgb_linear");
					if (hdr & EGL_HDR_CS_SCRGB_BIT)         appendExt("EGL_EXT_gl_colorspace_scrgb");
					if (hdr & EGL_HDR_CS_BT2020_PQ_BIT)     appendExt("EGL_EXT_gl_colorspace_bt2020_pq");
					if (hdr & EGL_HDR_CS_BT2020_LINEAR_BIT) appendExt("EGL_EXT_gl_colorspace_bt2020_linear");
					if (hdr & EGL_HDR_CS_BT2020_HLG_BIT)   appendExt("EGL_EXT_gl_colorspace_bt2020_hlg");
					if (hdr)
					{
						appendExt("EGL_EXT_surface_SMPTE2086_metadata");
						appendExt("EGL_EXT_surface_CTA861_3_metadata");
					}
					return extBuf;
				}
				break;
			}

			g_localStorage.error = EGL_BAD_PARAMETER;

			return 0;
		}

		walkerDpy = walkerDpy->next;
	}

	g_localStorage.error = EGL_BAD_DISPLAY;

	return 0;
}

EGLDisplay _eglGetPlatformDisplay(EGLenum platform, void *native_display, const EGLAttrib *attrib_list)
{
	(void)attrib_list;
	EGLNativeDisplayType nativeDisplay = EGL_DEFAULT_DISPLAY;
	if (!__matchPlatformDisplay(platform, native_display, &nativeDisplay))
	{
		g_localStorage.error = EGL_BAD_PARAMETER;
		return EGL_NO_DISPLAY;
	}
	return _eglGetDisplay(nativeDisplay);
}

EGLBoolean _eglReleaseThread(void)
{
	if (g_localStorage.currentCtx != EGL_NO_CONTEXT_IMPL)
	{
		auto _rl = g_globalStorage.placeRootDpy_readlock();
		EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

		while (walkerDpy)
		{
			guard_t _{ walkerDpy->mutex };

			if (walkerDpy->currentCtx == g_localStorage.currentCtx)
			{
				__makeCurrent(walkerDpy, nullptr, nullptr);
				walkerDpy->currentDraw = EGL_NO_SURFACE_IMPL;
				walkerDpy->currentRead = EGL_NO_SURFACE_IMPL;
				walkerDpy->currentCtx = EGL_NO_CONTEXT_IMPL;
				break;
			}

			walkerDpy = walkerDpy->next;
		}
	}

	g_localStorage = { EGL_SUCCESS, EGL_NONE, EGL_NO_CONTEXT_IMPL };

	return EGL_TRUE;
}

} // extern "C"
