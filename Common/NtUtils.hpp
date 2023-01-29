#pragma once

#include "NtBase.hpp"
#include "StrUtils.hpp"

// ---------------------------------------------------------------------------------------------------------------------

template <class T> void SafeRelease(T*& com_obj_ptr)
{
	if (com_obj_ptr)
	{
		com_obj_ptr->Release();
		com_obj_ptr = nullptr;
	}
}

// ---------------------------------------------------------------------------------------------------------------------

// Gets NT version and build number. 
// The upper 4 bits of build number are reserved for the type of the OS build.
// 0xC for a "checked" (or debug) build, and 0xF for a "free" (or retail) build.

extern "C" NTSYSAPI VOID WINAPI RtlGetNtVersionNumbers(
	__out_opt DWORD* pNtMajorVersion,
	__out_opt DWORD* pNtMinorVersion,
	__out_opt DWORD* pNtBuildNumber
);

inline uint32_t GetNtBuildNumber()
{
	static uint32_t build_number = 0;

	if (build_number == 0)
	{
		RtlGetNtVersionNumbers(NULL, NULL, (DWORD*)&build_number);
		build_number &= ~0xF0000000; // Clear type of OS build bits.
	}

	return build_number;
}

// ---------------------------------------------------------------------------------------------------------------------

struct RsrcSpan
{
	const uint8_t* data;
	size_t size;
	operator bool() const { return data != nullptr; }
};

inline RsrcSpan GetResource(HMODULE hModule, LPCTSTR lpName, LPCTSTR lpType)
{
	HRSRC hrsrc = FindResource(hModule, lpName, lpType);
	if (!hrsrc) { return { nullptr, 0 }; }
	HGLOBAL hglobal = LoadResource(hModule, hrsrc);
	if (!hglobal) { return { nullptr, 0 }; }
	return { (uint8_t*)LockResource(hglobal), SizeofResource(hModule, hrsrc) };
}

inline const VS_FIXEDFILEINFO* GetFixedVersion(HMODULE hModule = NULL)
{
	auto rsrc = GetResource(hModule, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
	if (!rsrc) { return nullptr; }

	// Parse the resource as the VS_VERSION_INFO pseudo structure to get the VS_FIXEDFILEINFO.

	if (rsrc.size < (40 + sizeof(VS_FIXEDFILEINFO))) { return nullptr; }
	if (*(uint16_t*)(rsrc.data + 0) < (40 + sizeof(VS_FIXEDFILEINFO))) { return nullptr; } // VS_VERSIONINFO::wLength
	if (*(uint16_t*)(rsrc.data + 2) != sizeof(VS_FIXEDFILEINFO)) { return nullptr; }       // VS_VERSIONINFO::wValueLength
	if (*(uint16_t*)(rsrc.data + 4) != 0) { return nullptr; }                              // VS_VERSIONINFO::wType (0 is binary, 1 is text)
	if (!StringEquals((wchar_t*)(rsrc.data + 6), L"VS_VERSION_INFO")) { return nullptr; }  // VS_VERSIONINFO::szKey
	const VS_FIXEDFILEINFO* ffi = (const VS_FIXEDFILEINFO*)(rsrc.data + 40);               // VS_VERSIONINFO::Value
	if (ffi->dwSignature != VS_FFI_SIGNATURE || ffi->dwStrucVersion != VS_FFI_STRUCVERSION) { return nullptr; }

	return ffi;
}

// ---------------------------------------------------------------------------------------------------------------------
