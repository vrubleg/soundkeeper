#pragma once

// Unified boolean `IS_*` definitions are always either `0` or `1`.

#if defined(__clang__)
	#define IS_MSVC         0
	#define IS_CLANG        1
	#define IS_GCC          0
#elif defined(__GNUC__)
	#define IS_MSVC         0
	#define IS_CLANG        0
	#define IS_GCC          1
#elif defined(_MSC_VER)
	#define IS_MSVC         1
	#define IS_CLANG        0
	#define IS_GCC          0
#else
	#error
#endif

#if defined(_M_IX86) || defined(__i386__)
	#define IS_32BIT        1
	#define IS_64BIT        0
	#define IS_X86          1
	#define IS_X8632        1
	#define IS_X8664        0
	#define IS_ARM          0
	#define IS_ARM32        0
	#define IS_ARM64        0
#elif defined(_M_X64) || defined(__x86_64__)
	#define IS_32BIT        0
	#define IS_64BIT        1
	#define IS_X86          1
	#define IS_X8632        0
	#define IS_X8664        1
	#define IS_ARM          0
	#define IS_ARM32        0
	#define IS_ARM64        0
#elif defined(_M_ARM) || defined(__arm__)
	#define IS_32BIT        1
	#define IS_64BIT        0
	#define IS_X86          0
	#define IS_X8632        0
	#define IS_X8664        0
	#define IS_ARM          1
	#define IS_ARM32        1
	#define IS_ARM64        0
#elif defined(_M_ARM64) || defined(__aarch64__)
	#define IS_32BIT        0
	#define IS_64BIT        1
	#define IS_X86          0
	#define IS_X8632        0
	#define IS_X8664        0
	#define IS_ARM          1
	#define IS_ARM32        0
	#define IS_ARM64        1
#else
	#error
#endif

#if defined(_M_ARM64EC)
	// Note that this mode is x86-64 compatible, so `IS_X8664` is also `1`.
	#define IS_ARM64EC      1
#else
	#define IS_ARM64EC      0
#endif

#if defined(_WIN32)
	#define IS_WIN          1
	#if defined(_KERNEL_MODE)
		#define IS_WIN_KM       1
		#define IS_WIN_UM       0
	#else
		#define IS_WIN_KM       0
		#define IS_WIN_UM       1
	#endif
	#if defined(_CONSOLE)
		#define IS_WIN_CUI      1
		#define IS_WIN_GUI      0
	#elif defined(_WINDOWS)
		#define IS_WIN_CUI      0
		#define IS_WIN_GUI      1
	#else
		#define IS_WIN_CUI      0
		#define IS_WIN_GUI      0
	#endif
#else
	#error
#endif

#if defined(_DEBUG)
	#define IS_DEBUG        1
#else
	#define IS_DEBUG        0
#endif

#if defined(_VC_NODEFAULTLIB)
	#define IS_NOCRT        1
#else
	#define IS_NOCRT        0
#endif

#if defined(__INTELLISENSE__)
	#define IS_INTELLISENSE 1
#else
	#define IS_INTELLISENSE 0
#endif

#define MAX_AUDIODEVICENAME_LENGTH 42 // the character limit for device names in mmsys.cpl is 41
