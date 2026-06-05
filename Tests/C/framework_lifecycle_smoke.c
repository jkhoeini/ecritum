#include <dlfcn.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef uint64_t ecritum_runtime_t;
typedef uint64_t ecritum_context_t;
typedef uint64_t ecritum_error_t;
typedef struct {
    const uint8_t *data;
    size_t len;
} ecritum_bytes_t;

typedef int (*ecritum_runtime_create_fn)(ecritum_bytes_t, ecritum_runtime_t *, ecritum_error_t *);
typedef int (*ecritum_runtime_destroy_fn)(ecritum_runtime_t *, ecritum_error_t *);
typedef int (*ecritum_context_create_fn)(ecritum_runtime_t, ecritum_bytes_t, ecritum_context_t *, ecritum_error_t *);
typedef int (*ecritum_context_destroy_fn)(ecritum_context_t *, ecritum_error_t *);

enum {
    ECRITUM_OK = 0,
};

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }

    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "%s\n", dlerror());
        return 3;
    }

    ecritum_runtime_create_fn runtime_create = (ecritum_runtime_create_fn)dlsym(handle, "ecritum_runtime_create");
    ecritum_runtime_destroy_fn runtime_destroy = (ecritum_runtime_destroy_fn)dlsym(handle, "ecritum_runtime_destroy");
    ecritum_context_create_fn context_create = (ecritum_context_create_fn)dlsym(handle, "ecritum_context_create");
    ecritum_context_destroy_fn context_destroy = (ecritum_context_destroy_fn)dlsym(handle, "ecritum_context_destroy");
    if (runtime_create == NULL || runtime_destroy == NULL || context_create == NULL || context_destroy == NULL) {
        fprintf(stderr, "%s\n", dlerror());
        return 4;
    }

    ecritum_bytes_t empty = {0};
    for (int i = 0; i < 100; i++) {
        ecritum_runtime_t runtime = 0;
        ecritum_context_t context = 0;
        ecritum_error_t error = 0;

        if (runtime_create(empty, &runtime, &error) != ECRITUM_OK || runtime == 0 || error != 0) {
            return 5;
        }
        if (context_create(runtime, empty, &context, &error) != ECRITUM_OK || context == 0 || error != 0) {
            return 6;
        }
        if (context_destroy(&context, &error) != ECRITUM_OK || context != 0 || error != 0) {
            return 7;
        }
        if (runtime_destroy(&runtime, &error) != ECRITUM_OK || runtime != 0 || error != 0) {
            return 8;
        }
    }

    return 0;
}
