#ifndef XRTRANSPORT_SERVER_C_API_H
#define XRTRANSPORT_SERVER_C_API_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// int error codes should be to errno values

typedef size_t (*ReadSomeDelegate)(void* cookie, void* buf, size_t size, int* ec);
typedef size_t (*WriteSomeDelegate)(void* cookie, const void* buf, size_t size, int* ec);
typedef void (*CloseDelegate)(void* cookie, int* ec);

/**
 * Runs an xrtransport server instance on the stream defined by the passed-in delegates.
 * 
 * Returns true if the server terminates gracefully, returns false if the handshake failed or any exception
 * was caught.
 */
bool xrtp_run_server(
    void* cookie,
    ReadSomeDelegate read_some,
    WriteSomeDelegate write_some,
    CloseDelegate close);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // XRTRANSPORT_SERVER_C_API_H