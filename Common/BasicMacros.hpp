#pragma once

// Mark a variable as unused explicitly to avoid warnings.
#define UNUSED(x) (void)(x)

// Stringify the argument without expanding macro definitions.
#define STRINGIFY_NX(x) #x

// Stringify the argument.
#define STRINGIFY(x) STRINGIFY_NX(x)

// Concatenate two tokens without expanding macro definitions.
#define TOKEN_CONCAT_NX(x, y) x ## y

// Concatenate two tokens.
#define TOKEN_CONCAT(x, y) TOKEN_CONCAT_NX(x, y)
