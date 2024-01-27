#pragma once

#include <stdint.h>
#include <stddef.h>

#define _WIN32_WINNT 0x0601
#include <sdkddkver.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOCOMM
#define NOKANJI
#define NOSOUND
#include <windows.h>
#include <objbase.h>

// ---------------------------------------------------------------------------------------------------------------------

// The best way to get HMODULE of current module, supported since VS2002.

EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define THIS_MODULE ((HMODULE)&__ImageBase)

// ---------------------------------------------------------------------------------------------------------------------

EXTERN_C_START

typedef _Success_(return >= 0) LONG NTSTATUS;
typedef NTSTATUS* PNTSTATUS;

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
#endif

typedef struct _UNICODE_STRING
{
	USHORT Length;
	USHORT MaximumLength;
	_Field_size_bytes_part_(MaximumLength, Length) PWCH Buffer;
} UNICODE_STRING, * PUNICODE_STRING;

typedef const UNICODE_STRING* PCUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES
{
	ULONG Length;
	HANDLE RootDirectory;
	PUNICODE_STRING ObjectName;
	ULONG Attributes;
	PVOID SecurityDescriptor; // PSECURITY_DESCRIPTOR;
	PVOID SecurityQualityOfService; // PSECURITY_QUALITY_OF_SERVICE
} OBJECT_ATTRIBUTES, * POBJECT_ATTRIBUTES;

typedef const OBJECT_ATTRIBUTES* PCOBJECT_ATTRIBUTES;

EXTERN_C_END

// ---------------------------------------------------------------------------------------------------------------------
