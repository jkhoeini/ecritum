#include "ecritum.h"

int main() {
    ecritum_runtime_t runtime = 0;
    ecritum_context_t context = 0;
    ecritum_namespace_t namespace_handle = 0;
    ecritum_function_t function = 0;
    ecritum_value_t value = 0;
    ecritum_call_t call = 0;
    ecritum_error_t error = 0;
    ecritum_bytes_t bytes = {0};
    ecritum_string_view_t view = {0};
    ecritum_host_fn_t callback = nullptr;
    ecritum_user_data_destroy_fn_t destroy_user_data = nullptr;

    (void)runtime;
    (void)context;
    (void)namespace_handle;
    (void)function;
    (void)value;
    (void)call;
    (void)error;
    (void)bytes;
    (void)view;
    (void)callback;
    (void)destroy_user_data;
    return ECRITUM_OK;
}
