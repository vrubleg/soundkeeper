#pragma once

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------------------------------------------------

constexpr char AsciiToLower(char c)
{
	return ('A' <= c && c <= 'Z') ? (c | 0b00100000) : c;
}

constexpr char AsciiToUpper(char c)
{
	return ('a' <= c && c <= 'z') ? (c & 0b11011111) : c;
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

constexpr int StringCompare(const char* l, const char* r)
{
	for (; *l && *l == *r; l++, r++);
	return (int)(uint8_t)*l - (int)(uint8_t)*r;
}

constexpr int StringCompareNoCase(const char* l, const char* r)
{
	for (; *l && AsciiToLower(*l) == AsciiToLower(*r); l++, r++);
	return (int)(uint8_t)AsciiToLower(*l) - (int)(uint8_t)AsciiToLower(*r);
}

constexpr int StringCompare(const wchar_t* l, const wchar_t* r)
{
	for (; *l && *l == *r; l++, r++);
	return (int)*l - (int)*r;
}

constexpr int StringCompareNoCase(const wchar_t* l, const wchar_t* r)
{
	for (; *l && AsciiToLower(*l) == AsciiToLower(*r); l++, r++);
	return (int)AsciiToLower(*l) - (int)AsciiToLower(*r);
}

// ---------------------------------------------------------------------------------------------------------------------

constexpr bool StringEquals(const char* l, const char* r)
{
	return StringCompare(l, r) == 0;
}

constexpr bool StringEqualsNoCase(const char* l, const char* r)
{
	return StringCompareNoCase(l, r) == 0;
}

constexpr bool StringEquals(const wchar_t* l, const wchar_t* r)
{
	return StringCompare(l, r) == 0;
}

constexpr bool StringEqualsNoCase(const wchar_t* l, const wchar_t* r)
{
	return StringCompareNoCase(l, r) == 0;
}

// ---------------------------------------------------------------------------------------------------------------------

constexpr const char* StringFindSub(const char* str, const char* sub)
{
	while (*str)
	{
		const char* curr_str = str;
		const char* curr_sub = sub;

		while (*curr_str && *curr_sub && *curr_str == *curr_sub)
		{
			curr_str++;
			curr_sub++;
		}

		if (!*curr_sub)
		{
			return str;
		}

		str++;
	}

	return nullptr;
}

constexpr const char* StringFindSubNoCase(const char* str, const char* sub)
{
	while (*str)
	{
		const char* curr_str = str;
		const char* curr_sub = sub;

		while (*curr_str && *curr_sub && AsciiToLower(*curr_str) == AsciiToLower(*curr_sub))
		{
			curr_str++;
			curr_sub++;
		}

		if (!*curr_sub)
		{
			return str;
		}

		str++;
	}

	return nullptr;
}

constexpr const wchar_t* StringFindSub(const wchar_t* str, const wchar_t* sub)
{
	while (*str)
	{
		const wchar_t* curr_str = str;
		const wchar_t* curr_sub = sub;

		while (*curr_str && *curr_sub && *curr_str == *curr_sub)
		{
			curr_str++;
			curr_sub++;
		}

		if (!*curr_sub)
		{
			return str;
		}

		str++;
	}

	return nullptr;
}

constexpr const wchar_t* StringFindSubNoCase(const wchar_t* str, const wchar_t* sub)
{
	while (*str)
	{
		const wchar_t* curr_str = str;
		const wchar_t* curr_sub = sub;

		while (*curr_str && *curr_sub && AsciiToLower(*curr_str) == AsciiToLower(*curr_sub))
		{
			curr_str++;
			curr_sub++;
		}

		if (!*curr_sub)
		{
			return str;
		}

		str++;
	}

	return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------

constexpr bool StringContains(const char* str, const char* sub)
{
	return StringFindSub(str, sub) != nullptr;
}

constexpr bool StringContainsNoCase(const char* str, const char* sub)
{
	return StringFindSubNoCase(str, sub) != nullptr;
}

constexpr bool StringContains(const wchar_t* str, const wchar_t* sub)
{
	return StringFindSub(str, sub) != nullptr;
}

constexpr bool StringContainsNoCase(const wchar_t* str, const wchar_t* sub)
{
	return StringFindSubNoCase(str, sub) != nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
