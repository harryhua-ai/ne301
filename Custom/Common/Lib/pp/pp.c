#include <string.h>
#include "pp.h"

// Implemented entries
extern const pp_entry_t pp_entry_od_st_yolox_uf;
extern const pp_entry_t pp_entry_mpe_yolo_v8_uf;
extern const pp_entry_t pp_entry_od_yolo_v8_uf;
extern const pp_entry_t pp_entry_od_yolo_v8_ui;

/* Return registry (pointer array) and count */
static const pp_entry_t * const* get_registered_entries(size_t *out_count)
{
    /* Local static array: element type is "pointer to const pp_entry_t" */
    static const pp_entry_t * const entries[] = {
        &pp_entry_od_st_yolox_uf,
        &pp_entry_mpe_yolo_v8_uf,
        &pp_entry_od_yolo_v8_uf,
        &pp_entry_od_yolo_v8_ui,
    };
    if (out_count) {
        *out_count = sizeof(entries) / sizeof(entries[0]);
    }
    return entries;
}

const pp_vtable_t* pp_find(const char *name)
{
    if (!name) return NULL;

    size_t count = 0;
    const pp_entry_t * const* entries = get_registered_entries(&count);

    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i]->name, name) == 0) {
            return entries[i]->vt;
        }
    }
    return NULL;
}

/* Other functions remain unchanged */
int32_t pp_init(void) { return 0; }
void pp_deinit(void) { }
