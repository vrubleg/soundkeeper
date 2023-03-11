#pragma once

#include "BasicMacros.hpp"

template <typename T>
struct Deferrer
{
	T f;
	Deferrer(T f) : f(f) { };
	Deferrer(const Deferrer&) = delete;
	~Deferrer() { f(); }
};

// Defer execution of the following lambda to the end of current scope.
#define defer Deferrer TOKEN_CONCAT(__deferred, __COUNTER__) =
