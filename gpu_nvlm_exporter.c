#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <nvml.h>
#include <microhttpd.h>

#define PORT 8080

// Data
double gpu_power_usage = 0.0;
unsigned int gpu_temperature = 0;
unsigned int gpu_utilization = 0;
unsigned int gpu_mem_utilization = 0;

// GET /metrics
static enum MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                                            const char *url, const char *method,
                                            const char *version, const char *upload_data,
                                            size_t *upload_data_size, void **con_cls) {
    if (strcmp(url, "/metrics") != 0) {
        return MHD_NO; // Only respond to /metrics
    }

    char response_str[1024];
    
    // Prometheus Text Format:
    snprintf(response_str, sizeof(response_str),
             "# HELP gpu_power_usage Power usage of the GPU in Watts\n"
             "# TYPE gpu_power_usage gauge\n"
             "gpu_power_usage %.2f\n"
             "# HELP gpu_temperature Temperature of the GPU in Celsius\n"
             "# TYPE gpu_temperature gauge\n"
             "gpu_temperature %u\n"
             "# HELP gpu_utilization GPU Engine Utilization in percent\n"
             "# TYPE gpu_utilization gauge\n"
             "gpu_utilization %u\n"
             "# HELP gpu_mem_utilization GPU Memory Utilization in percent\n"
             "# TYPE gpu_mem_utilization gauge\n"
             "gpu_mem_utilization %u\n",
             gpu_power_usage, gpu_temperature, gpu_utilization, gpu_mem_utilization);

    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(response_str), (void *)response_str, MHD_RESPMEM_MUST_COPY);
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    
    return ret;
}

// Polling loop:
void* nvml_worker_thread(void* arg) {
    nvmlReturn_t result;
    nvmlDevice_t device;

    // Init NVML
    result = nvmlInit();
    if (NVML_SUCCESS != result) {
        printf("Failed to initialize NVML: %s\n", nvmlErrorString(result));
        exit(1);
    }

    // Device 0 (or other Device by change the Index)
    // nvmlDeviceGetCount() to see number of GPU devices
    result = nvmlDeviceGetHandleByIndex(0, &device);
    if (NVML_SUCCESS != result) {
        printf("Failed to get device handle: %s\n", nvmlErrorString(result));
        exit(1);
    }

    printf("NVML Worker started. Polling device 0...\n");

    while (1) {
        unsigned int power_mw = 0;
        if (NVML_SUCCESS == nvmlDeviceGetPowerUsage(device, &power_mw)) {
            gpu_power_usage = (double)power_mw / 1000.0;
        }

        unsigned int temp = 0;
        if (NVML_SUCCESS == nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp)) {
            gpu_temperature = temp;
        }

        nvmlUtilization_t util = {0, 0};
        if (NVML_SUCCESS == nvmlDeviceGetUtilizationRates(device, &util)) {
            gpu_utilization = util.gpu;
            gpu_mem_utilization = util.memory;
        }

        sleep(1);
    }

    nvmlShutdown();
    return NULL;
}

int main() {
    pthread_t worker_id;
    
    // Create thread for loop:
    if (pthread_create(&worker_id, NULL, nvml_worker_thread, NULL) != 0) {
        printf("Failed to create NVML thread.\n");
        return 1;
    }

    // Create HTTP Server for Prometheus by libmicrohttpd:
    struct MHD_Daemon *daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
                                                 &answer_to_connection, NULL, MHD_OPTION_END);
    if (NULL == daemon) {
        printf("Failed to start HTTP server.\n");
        return 1;
    }

    printf("Prometheus Exporter running on http://localhost:%d/metrics\n", PORT);
    printf("Press Enter to stop...\n");
    
    getchar(); // Enter to terminate

    MHD_stop_daemon(daemon);
    return 0;
}