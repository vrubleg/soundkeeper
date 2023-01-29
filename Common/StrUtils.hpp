#pragma once

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------------------------------------------------

using TransformByteCharFuncType = char (*)(char);
using TransformWideCharFuncType = wchar_t (*)(wchar_t);

// ---------------------------------------------------------------------------------------------------------------------

constexpr char NoTransform(char c)
{
	return c;
}

constexpr char AsciiToLower(char c)
{
	return ('A' <= c && c <= 'Z') ? (c | 0b00100000) : c;
}

constexpr char AsciiToUpper(char c)
{
	return ('a' <= c && c <= 'z') ? (c & 0b11011111) : c;
}

constexpr wchar_t NoTransform(wchar_t c)
{
	return c;
}

constexpr wchar_t AsciiToLower(wchar_t c)
{
	return (L'A' <= c && c <= L'Z') ? (c | 0b0000000000100000) : c;
}

constexpr wchar_t AsciiToUpper(wchar_t c)
{
	return (L'a' <= c && c <= L'z') ? (c & 0b1111111111011111) : c;
}

// ---------------------------------------------------------------------------------------------------------------------

template <TransformByteCharFuncType TransformChar = NoTransform>
constexpr int StringCompare(const char* l, const char* r)
{
	for (; *l && TransformChar(*l) == TransformChar(*r); l++, r++);
	return (int)(uint8_t)TransformChar(*l) - (int)(uint8_t)TransformChar(*r);
}

template <TransformWideCharFuncType TransformChar = NoTransform>
constexpr int StringCompare(const wchar_t* l, const wchar_t* r)
{
	for (; *l && TransformChar(*l) == TransformChar(*r); l++, r++);
	return (int)TransformChar(*l) - (int)TransformChar(*r);
}

// ---------------------------------------------------------------------------------------------------------------------

template <TransformByteCharFuncType TransformChar = NoTransform>
constexpr bool StringEquals(const char* l, const char* r)
{
	return StringCompare<TransformChar>(l, r) == 0;
}

template <TransformWideCharFuncType TransformChar = NoTransform>
constexpr bool StringEquals(const wchar_t* l, const wchar_t* r)
{
	return StringCompare<TransformChar>(l, r) == 0;
}

// ---------------------------------------------------------------------------------------------------------------------

template <TransformByteCharFuncType TransformChar = NoTransform>
constexpr const char* StringFindPtr(const char* str, const char* sub)
{
	while (*str)
	{
		const char *l = str; const char *r = sub;
		while (*l && TransformChar(*l) == TransformChar(*r)) { l++; r++; }
		if (!*r) { return str; }
		str++;
	}

	return nullptr;
}

template <TransformWideCharFuncType TransformChar = NoTransform>
constexpr const wchar_t* StringFindPtr(const wchar_t* str, const wchar_t* sub)
{
	while (*str)
	{
		const wchar_t* l = str; const wchar_t* r = sub;
		while (*l && TransformChar(*l) == TransformChar(*r)) { l++; r++; }
		if (!*r) { return str; }
		str++;
	}

	return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------

template <TransformByteCharFuncType TransformChar = NoTransform>
constexpr size_t StringIndexOf(const char* str, const char* sub)
{
	auto ptr = StringFindPtr<TransformChar>(str, sub);
	return ptr ? (ptr - str) : -1;
}

template <TransformWideCharFuncType TransformChar = NoTransform>
constexpr size_t StringIndexOf(const wchar_t* str, const wchar_t* sub)
{
	auto ptr = StringFindPtr<TransformChar>(str, sub);
	return ptr ? (ptr - str) : -1;
}

// ---------------------------------------------------------------------------------------------------------------------

template <TransformByteCharFuncType TransformChar = NoTransform>
constexpr bool StringContains(const char* str, const char* sub)
{
	return StringFindPtr<TransformChar>(str, sub) != nullptr;
}

template <TransformWideCharFuncType TransformChar = NoTransform>
constexpr bool StringContains(const wchar_t* str, const wchar_t* sub)
{
	return StringFindPtr<TransformChar>(str, sub) != nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
