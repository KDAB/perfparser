
#include "swift_demangler.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <dlfcn.h>
#include <unistd.h>

// Function pointer type for swift_demangle.
typedef char *(*swift_demangle_func_t)(const char *mangledName, size_t mangledNameLength,
                                         char *outputBuffer, size_t *outputBufferSize,
                                         uint32_t flags);

// Static variables to hold the library handle and function pointer.
static swift_demangle_func_t swift_demangle_ptr = NULL;
static void *swift_demangle_handle = NULL;

static int init_swift_demangle(void) {
    if (swift_demangle_ptr)
        return 1; // Already initialized.

    // Get the PATH environment variable.
    char *path_env = getenv("PATH");
    if (!path_env) {
        return 0;
    }

    // Duplicate PATH because strtok_r modifies the string.
    char *path_dup = strdup(path_env);
    if (!path_dup) {
        return 0;
    }

    // Search for "swiftc" in each directory of PATH.
    char swift_path[PATH_MAX];
    int found = 0;
    char *saveptr = NULL;
    for (char *dir = strtok_r(path_dup, ":", &saveptr);
         dir != NULL;
         dir = strtok_r(NULL, ":", &saveptr)) {
        snprintf(swift_path, sizeof(swift_path), "%s/swift", dir);
        if (access(swift_path, X_OK) == 0) {
            found = 1;
            break;
        }
    }
    free(path_dup);
    if (!found) {
        return 0;
    }

    // Expect swift_path to be of the form SOMEPATH/usr/bin/swift.
    // Locate the "/usr/bin/swift" substring.
    const char *needle = "/usr/bin/swift";
    char *substr = strstr(swift_path, needle);
    if (!substr) {
        return 0;
    }

    // The installation root is everything preceding "/usr/bin/swift".
    size_t base_len = substr - swift_path;
    char base_path[PATH_MAX];
    if (base_len >= sizeof(base_path)) {
        return 0;
    }
    strncpy(base_path, swift_path, base_len);
    base_path[base_len] = '\0';

    // Construct the full path to libswiftDemangle.so:
    //   SOMEPATH/usr/lib/libswiftDemangle.so
    char lib_path[PATH_MAX];
    snprintf(lib_path, sizeof(lib_path), "%s/usr/lib/libswiftDemangle.so", base_path);

    // Load the dynamic library.
    swift_demangle_handle = dlopen(lib_path, RTLD_LAZY);
    if (!swift_demangle_handle) {
        fprintf(stderr, "dlopen error: %s\n", dlerror());
        return 0;
    }

    // Get the address of swift_demangle.
    swift_demangle_ptr = (swift_demangle_func_t)dlsym(swift_demangle_handle, "swift_demangle");
    if (!swift_demangle_ptr) {
        dlclose(swift_demangle_handle);
        swift_demangle_handle = NULL;
        return 0;
    }

    return 1;
}

/*
 * demangle:
 *   Attempts to demangle the given Swift symbol using libSwiftDemangle.
 *
 * Parameters:
 *   symbol       - The mangled Swift symbol.
 *   buffer       - A caller-provided buffer to hold the demangled name, but only
 *                  if demangling actually occurs.
 *   bufferLength - The size of the provided buffer.
 *
 * Returns:
 *   1 if demangling occurred (i.e. the demangled name differs from the original symbol),
 *   0 otherwise.
 *
 * Note:
 *   If the library or function cannot be loaded, or the symbol was not mangled,
 *   the buffer remains unmodified.
 */
int swift_demangle(const char* symbol, char* buffer, size_t bufferLength) {
    // Avoid in-place modification of the input symbol.
    if (buffer == symbol) {
        return 0;
    }

    // Initialize and load libswiftDemangle if necessary.
    if (!swift_demangle_ptr && !init_swift_demangle()) {
         return 0;
    }

    size_t symbolLength = strlen(symbol);
    size_t outBufferSize = bufferLength;
    uint32_t flags = 0; // Use default (compact) demangling options.

    // Call swift_demangle via the function pointer.
    char *result = swift_demangle_ptr(symbol, symbolLength, buffer, &outBufferSize, flags);
    if (result == NULL) {
        // Error occurred; do not modify buffer.
        return 0;
    }
    if (result == symbol) {
        // The symbol was not mangled; do not modify buffer.
        return 0;
    }

    // If the demangled result was not written into the provided buffer,
    // copy it now and free the allocated memory.
    if (result != buffer) {
        strncpy(buffer, result, bufferLength);
        buffer[bufferLength - 1] = '\0';
        free(result);
    }
    return 1;
}
