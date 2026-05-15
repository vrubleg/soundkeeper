// We use msvcrt.dll as a C runtime to save space in release builds.
// It has limited set of functions, so we reimplement missing ones here.

#if (defined(_VC_NODEFAULTLIB) || defined(__INTELLISENSE__)) && defined(_M_IX86)

// ---------------------------------------------------------------------------------------------------------------------

//
// MSVC x86 CRT helper for converting a floating-point value (double) to a signed 64-bit integer (int64_t).
//
// Uses custom calling convention:
// - Input:  value is passed in FPU register ST0
// - Output: result is returned in EDX:EAX
//

extern "C" void __declspec(naked) _ftol2()
{
	__asm
	{
		sub     esp, 8

		// Round ST0 into a 64-bit integer at [ESP], and then load it in the EDX:EAX. There is no non-pop 64-bit FIST,
		// so duplicate the value and use FISTP for this purpose.

		fld     st(0)                       // ST1 = ST0 = original
		fistp   qword ptr [esp]             // [ESP] = rounded, ST0 = original
		mov     eax, dword ptr [esp]
		mov     edx, dword ptr [esp+4]

		// Need to apply a correction to enforce truncation toward zero, independent of the current FPU rounding mode,
		// if (EDX:EAX & 0x7FFFFFFFFFFFFFFF) != 0.

		test    eax, eax
		jne     needs_correction
		test    edx, 7FFFFFFFh
		jne     needs_correction

		// The result is either 0 or 0x8000000000000000. The latter is ambiguous: it may be a valid INT64_MIN or the x87
		// "integer indefinite" value produced for NaN or overflow. In both cases, return the result as is.

		fstp    st(0)
		jmp     cleanup

	needs_correction:

		// Calculate the difference between the original value and the rounded result.

		fild    qword ptr [esp]             // ST0 = rounded, ST1 = original
		fsubr   st(0), st(1)                // ST0 = diff = original - rounded, ST1 = original

		// Check sign of the original value.

		fxch    st(1)                       // ST0 = original, ST1 = diff
		fstp    dword ptr [esp]
		mov     ecx, dword ptr [esp]
		test    ecx, ecx

		// Store the difference into ECX (does not affect EFLAGS).

		fstp    dword ptr [esp]
		mov     ecx, dword ptr [esp]

		jns     is_positive

	// is_negative:

		// Correction for negative values.

		xor     ecx, 80000000h
		add     ecx, 7FFFFFFFh
		adc     eax, 0
		adc     edx, 0
		jmp     cleanup

	is_positive:

		// Correction for positive values.

		add     ecx, 7FFFFFFFh
		sbb     eax, 0
		sbb     edx, 0

	cleanup:

		add     esp, 8
		ret
	}
}

// ---------------------------------------------------------------------------------------------------------------------

//
// MSVC x86 CRT helper for converting a floating-point value (double) to an unsigned 64-bit integer (uint64_t).
//
// Uses custom calling convention:
// - Input:  value is passed in FPU register ST0
// - Output: result is returned in EDX:EAX
//
// _ftoul2_legacy - version for "/fpcvt:BC" that returns 0x8000000000000000 as the "indefinite" value (used by default).
// _ftoul2 - version for "/fpcvt:IA" that returns 0xFFFFFFFFFFFFFFFF as the "indefinite" value (consistent with AVX512).
//

extern "C" void __declspec(naked) _ftoul2_legacy()
{
	static const auto LLONG_MAX_PLUS_ONE = 0x43e0000000000000ULL;

	__asm
	{
		fld     qword ptr [LLONG_MAX_PLUS_ONE]
		fcom
		fnstsw  ax
		test    ah, 41h
		jnp     greater_than_int64

		// if 0 <= x < 2^63: return int64_t(x)

		fstp    st(0)
		jmp     _ftol2

	greater_than_int64:

		// if 2^63 <= x < 2^64: return int64_t(x - 2^63) + 2^63

		fsub    st(1), st(0)
		fcomp
		fnstsw  ax
		test    ah, 41h
		jnz     greater_than_uint64

		call    _ftol2
		add     edx, 80000000h
		ret

	greater_than_uint64:

		// if (x >= 2^64): return 0x8000000000000000

		xor     eax, eax
		mov     edx, 80000000h
		ret
	}
}

extern "C" void __declspec(naked) _ftoul2()
{
	static const auto LLONG_MAX_PLUS_ONE = 0x43e0000000000000ULL;

	__asm
	{
		fld     qword ptr [LLONG_MAX_PLUS_ONE]
		fcom
		fnstsw  ax
		test    ah, 41h
		jnp     greater_than_int64

		// if 0 <= x < 2^63: return int64_t(x)

		fstp    st(0)
		jmp     _ftol2

	greater_than_int64:

		// if 2^63 <= x < 2^64: return int64_t(x - 2^63) + 2^63

		fsub    st(1), st(0)
		fcomp
		fnstsw  ax
		test    ah, 41h
		jnz     greater_than_uint64

		call    _ftol2
		add     edx, 80000000h
		ret

	greater_than_uint64:

		// if (x >= 2^64): return 0xFFFFFFFFFFFFFFFF

		or      eax, 0FFFFFFFFh
		or      edx, 0FFFFFFFFh
		ret
	}
}

// ---------------------------------------------------------------------------------------------------------------------

#endif
