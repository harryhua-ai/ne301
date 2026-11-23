/* Host-test stub for Appli/Core/Inc/common_utils.h.
 * The firmware uses IN_PSRAM/ALIGN_32 for the docsnap worker stack
 * (docsnap_worker_stack); on the host neither the section nor the large
 * alignment has meaning, so both collapse to nothing. */
#ifndef HOST_STUB_COMMON_UTILS_H
#define HOST_STUB_COMMON_UTILS_H

#define ALIGN_32
#define IN_PSRAM

#endif /* HOST_STUB_COMMON_UTILS_H */
