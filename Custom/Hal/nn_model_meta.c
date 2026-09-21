#include "nn_model_meta.h"
#include <string.h>
#include "cJSON.h"

int nn_parse_model_config(const char *config_json, nn_config_meta_t *meta)
{
    if (!config_json || !meta) {
        return -1;
    }

    memset(meta, 0, sizeof(*meta));

    cJSON *root = cJSON_Parse(config_json);
    if (root == NULL) {
        return -1;
    }

    cJSON *mi = cJSON_GetObjectItemCaseSensitive(root, "model_info");
    if (cJSON_IsObject(mi)) {
        cJSON *type = cJSON_GetObjectItemCaseSensitive(mi, "type");
        if (cJSON_IsString(type) && type->valuestring != NULL) {
            strncpy(meta->model_type, type->valuestring, sizeof(meta->model_type) - 1);
            meta->model_type[sizeof(meta->model_type) - 1] = '\0';
        }
    }

    cJSON *pp = cJSON_GetObjectItemCaseSensitive(root, "postprocess_params");
    if (!cJSON_IsObject(pp)) {
        cJSON_Delete(root);
        return 0;
    }

    cJSON *nc = cJSON_GetObjectItemCaseSensitive(pp, "num_classes");
    cJSON *cn = cJSON_GetObjectItemCaseSensitive(pp, "class_names");
    if (!cJSON_IsNumber(nc) || !cJSON_IsArray(cn)) {
        cJSON_Delete(root);
        return 0;
    }

    int declared = (int)nc->valuedouble;
    int nb_names = cJSON_GetArraySize(cn);
    if (declared <= 0 || declared > NN_MAX_CLASS_COUNT || declared != nb_names) {
        cJSON_Delete(root);
        return 0;
    }

    for (int i = 0; i < declared; ++i) {
        cJSON *item = cJSON_GetArrayItem(cn, i);
        if (!cJSON_IsString(item) || item->valuestring == NULL ||
            item->valuestring[0] == '\0' ||
            strlen(item->valuestring) >= NN_CLASS_NAME_MAX) {
            cJSON_Delete(root);
            return 0;
        }
    }

    meta->num_classes = (uint16_t)declared;
    meta->classes.count = (uint16_t)declared;
    for (int i = 0; i < declared; ++i) {
        cJSON *item = cJSON_GetArrayItem(cn, i);
        strncpy(meta->classes.names[i], item->valuestring, NN_CLASS_NAME_MAX - 1);
        meta->classes.names[i][NN_CLASS_NAME_MAX - 1] = '\0';
    }

    cJSON_Delete(root);
    return 0;
}

int nn_class_list_get(const nn_class_list_t *list, uint16_t index, char *buf, size_t buf_size)
{
    if (!list || !buf || buf_size == 0 || index >= list->count) {
        return -1;
    }

    size_t len = strlen(list->names[index]);
    if (len >= buf_size) {
        return -1;
    }

    memcpy(buf, list->names[index], len + 1);
    return 0;
}

uint32_t nn_generation_next(uint32_t current)
{
    return current + 1u;
}
