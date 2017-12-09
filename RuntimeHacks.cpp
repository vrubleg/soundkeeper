#ifdef NDEBUG

// "?terminate@@YAXXZ" from "msvcrt.dll" will be used
void __cdecl terminate(void);

// Helper function referenced by the front-end to assist in implementing noexcept.
extern "C" __declspec(noreturn) void __cdecl __std_terminate()
{
	terminate();
}

// Fallbacks the scalar operator delete with size_t
void __cdecl operator delete(void* const block, size_t const) noexcept
{
	operator delete(block);
}

#endif
