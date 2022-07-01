// We use msvcrt.dll as a C runtime to save space in release builds.
// It has limited set of functions, so we reimplement missing ones here.

#if !defined(_CONSOLE) && _MSC_VER >= 1900 /* VS2015+ */

// Fallback for the sized delete (introduced in C++14).
void __cdecl operator delete(void* const block, size_t const) noexcept
{
	operator delete(block);
}

#endif
