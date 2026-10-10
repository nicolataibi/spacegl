#include <stdint.h>
#include <stddef.h>
#include "packets.h"
#include "server_internal.h"
#include "network.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < sizeof(int)) return 0;
    
    // Simulate reading a packet
    // Since process_command and dispatch_packet usually take internal structures
    // we can mock a connection context or pass directly if exposed.
    // For now, let's assume we can call some exposed packet handling.
    // As a simple harness, we just feed data to dispatch_packet if we can mock io_context
    return 0;
}
