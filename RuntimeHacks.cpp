#if !defined(_CONSOLE) && _MSC_VER >= 1900 /* VS2015+ */

// Fallbacks the scalar operator delete with size_t
void __cdecl operator delete(void* const block, size_t const) noexcept
{
	operator delete(block);
}

#endif
