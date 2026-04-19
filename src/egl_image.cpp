#include "egl_common.h"

extern "C"
{

EGLImage _eglCreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLAttrib *attrib_list)
{
	(void)ctx; (void)attrib_list;
	switch (target)
	{
	case EGL_GL_TEXTURE_2D:
	case EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_X:
	case EGL_GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
	case EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
	case EGL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
	case EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
	case EGL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
	case EGL_GL_TEXTURE_3D:
	case 0x30C3: // EGL_GL_TEXTURE_2D_ARRAY (EGL 1.5)
	case EGL_GL_RENDERBUFFER:
		break;
	default:
		g_localStorage.error = EGL_BAD_PARAMETER;
		return EGL_NO_IMAGE;
	}

	if (!buffer)
	{
		g_localStorage.error = EGL_BAD_PARAMETER;
		return EGL_NO_IMAGE;
	}

	auto _rl = g_globalStorage.placeRootDpy_readlock();
	EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;
	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
			if (!walkerDpy->initialized || walkerDpy->destroy)
			{
				g_localStorage.error = EGL_NOT_INITIALIZED;
				return EGL_NO_IMAGE;
			}

			EGLImageImpl* newImage = new EGLImageImpl();
			if (!newImage)
			{
				g_localStorage.error = EGL_BAD_ALLOC;
				return EGL_NO_IMAGE;
			}
			newImage->target = target;
			newImage->buffer = buffer;
			std::lock_guard<std::mutex> lk(walkerDpy->mutex);
			newImage->next      = walkerDpy->rootImage;
			walkerDpy->rootImage = newImage;
			return (EGLImage)newImage;
		}
		walkerDpy = walkerDpy->next;
	}
	g_localStorage.error = EGL_BAD_DISPLAY;
	return EGL_NO_IMAGE;
}

EGLBoolean _eglDestroyImage(EGLDisplay dpy, EGLImage image)
{
	auto _rl = g_globalStorage.placeRootDpy_readlock();
	EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;
	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
			if (!walkerDpy->initialized || walkerDpy->destroy)
			{
				g_localStorage.error = EGL_NOT_INITIALIZED;
				return EGL_FALSE;
			}
			std::lock_guard<std::mutex> lk(walkerDpy->mutex);
			EGLImageImpl* prev = nullptr;
			EGLImageImpl* walker = walkerDpy->rootImage;
			while (walker)
			{
				if ((EGLImage)walker == image)
				{
					if (prev)
						prev->next = walker->next;
					else
						walkerDpy->rootImage = walker->next;
					delete walker;
					return EGL_TRUE;
				}
				prev   = walker;
				walker = walker->next;
			}
			g_localStorage.error = EGL_BAD_PARAMETER;
			return EGL_FALSE;
		}
		walkerDpy = walkerDpy->next;
	}
	g_localStorage.error = EGL_BAD_DISPLAY;
	return EGL_FALSE;
}

} // extern "C"
