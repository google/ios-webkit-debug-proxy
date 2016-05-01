#if defined(_MSC_VER)

// Define ssize_t (as SSIZE_T)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;

// The 'bool' type is used by various files, so include it.
#include <stdbool.h>
#endif