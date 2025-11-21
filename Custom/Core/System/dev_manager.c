#include "dev_manager.h"

// Global device manager
static dev_mgr_t g_dev_mgr;

// Thread safety macros
#define LOCK(mgr) do { if ((mgr)->thread_safe) (mgr)->lock(); } while(0)
#define UNLOCK(mgr) do { if ((mgr)->thread_safe) (mgr)->unlock(); } while(0)

#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); })

#define INIT_LIST_HEAD(ptr) do { \
    (ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

#define list_for_each_entry(pos, head, member) \
    for (pos = container_of((head)->next, typeof(*pos), member); \
         &(pos)->member != (head); \
         pos = container_of((pos)->member.next, typeof(*pos), member))

#define list_for_each_entry_safe(pos, n, head, member) \
    for (pos = container_of((head)->next, typeof(*pos), member), \
         n = container_of(pos->member.next, typeof(*pos), member); \
         &pos->member != (head); \
         pos = n, n = container_of(n->member.next, typeof(*n), member))

static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
    new->next = head;
    new->prev = head->prev;
    head->prev->next = new;
    head->prev = new;
}

static inline void list_del(struct list_head *entry)
{
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
    entry->next = NULL;
    entry->prev = NULL;
}

// Device registration
int device_register(device_t *dev)
{
    if (!dev || dev->type >= DEV_TYPE_MAX) 
        return -1;

    LOCK(&g_dev_mgr);
    
    // Check if device name already exists
    device_t *pos;
    list_for_each_entry(pos, &g_dev_mgr.devices[dev->type], list) {
        if (strcmp(pos->name, dev->name) == 0) {
            UNLOCK(&g_dev_mgr);
            return -2; // Device name conflict
        }
    }
    
    // Initialize device
    if (dev->ops && dev->ops->init) {
        int ret = dev->ops->init(dev->priv_data);
        if (ret != 0) {
            UNLOCK(&g_dev_mgr);
            return ret;
        }
    }
    
    // Add to device list
    list_add_tail(&dev->list, &g_dev_mgr.devices[dev->type]);

    UNLOCK(&g_dev_mgr);
    return 0;
}

// Device unregistration
void device_unregister(device_t *dev)
{
    if (!dev || dev->type >= DEV_TYPE_MAX) 
        return;

    LOCK(&g_dev_mgr);
    
    // Execute device deinitialization
    if (dev->ops && dev->ops->deinit) {
        dev->ops->deinit(dev->priv_data);
    }
    
    // Remove from list
    list_del(&dev->list);
    
    UNLOCK(&g_dev_mgr);
}

// Find device by name
device_t *device_find(const char *name, dev_type_t type)
{
    if (!name || type >= DEV_TYPE_MAX) 
        return NULL;

    device_t *dev = NULL;
    LOCK(&g_dev_mgr);
    
    list_for_each_entry(dev, &g_dev_mgr.devices[type], list) {
        if (strcmp(dev->name, name) == 0) {
            UNLOCK(&g_dev_mgr);
            return dev;
        }
    }
    
    UNLOCK(&g_dev_mgr);
    return NULL;
}

// Device operation API
int device_open(device_t *dev)
{
    return dev->ops && dev->ops->open ? dev->ops->open(dev->priv_data) : -1;
}

int device_close(device_t *dev)
{
    return dev->ops && dev->ops->close ? dev->ops->close(dev->priv_data) : -1;
}

int device_start(device_t *dev)
{
    return dev->ops && dev->ops->start ? dev->ops->start(dev->priv_data) : -1;
}

int device_stop(device_t *dev)
{
    return dev->ops && dev->ops->stop ? dev->ops->stop(dev->priv_data) : -1;
}

int device_ioctl(device_t *dev, unsigned int cmd, unsigned char* ubuf, unsigned long arg)
{
    return dev->ops && dev->ops->ioctl ? dev->ops->ioctl(dev->priv_data, cmd, ubuf, arg) : -1;
}

int device_foreach(device_callback_t callback, void *arg)
{
    int count = 0;
    if (!callback) return -1;
    
    LOCK(&g_dev_mgr);
    
    for (int type = 0; type < DEV_TYPE_MAX; type++) {
        device_t *dev;
        list_for_each_entry(dev, &g_dev_mgr.devices[type], list) {
            callback(dev, arg);
            count++;
        }
    }
    
    UNLOCK(&g_dev_mgr);
    return count;
}

// Traverse devices of specific type
int device_foreach_type(dev_type_t type, device_callback_t callback, void *arg)
{
    int count = 0;
    if (type >= DEV_TYPE_MAX || !callback) return -1;
    
    LOCK(&g_dev_mgr);
    
    device_t *dev;
    list_for_each_entry(dev, &g_dev_mgr.devices[type], list) {
        callback(dev, arg);
        count++;
    }
    
    UNLOCK(&g_dev_mgr);
    return count;
}

// Get device count
int device_count(dev_type_t type)
{
    int count = 0;
    
    LOCK(&g_dev_mgr);
    
    if (type < DEV_TYPE_MAX) {
        device_t *dev;
        list_for_each_entry(dev, &g_dev_mgr.devices[type], list) {
            count++;
        }
    } else {
        // Total count of all device types
        for (int i = 0; i < DEV_TYPE_MAX; i++) {
            device_t *dev;
            list_for_each_entry(dev, &g_dev_mgr.devices[i], list) {
                count++;
            }
        }
    }
    
    UNLOCK(&g_dev_mgr);
    return count;
}

// Find device by name pattern
device_t *device_find_pattern(const char *pattern, dev_type_t type)
{
    if (!pattern) return NULL;
    
    device_t *result = NULL;
    LOCK(&g_dev_mgr);
    
    if (type < DEV_TYPE_MAX) {
        device_t *dev;
        list_for_each_entry(dev, &g_dev_mgr.devices[type], list) {
            if (strstr(dev->name, pattern)) {
                result = dev;
                break;
            }
        }
    } else {
        // Search in all types
        for (int i = 0; i < DEV_TYPE_MAX; i++) {
            device_t *dev;
            list_for_each_entry(dev, &g_dev_mgr.devices[i], list) {
                if (strstr(dev->name, pattern)) {
                    result = dev;
                    goto found;
                }
            }
        }
    }
    
found:
    UNLOCK(&g_dev_mgr);
    return result;
}

// Initialize device manager
void device_manager_init(dev_lock_func_t lock, dev_unlock_func_t unlock)
{
    for (int i = 0; i < DEV_TYPE_MAX; i++) {
        INIT_LIST_HEAD(&g_dev_mgr.devices[i]);
    }
    
    /* Set thread safety callbacks */
    if (lock && unlock) {
        g_dev_mgr.lock = lock;
        g_dev_mgr.unlock = unlock;
        g_dev_mgr.thread_safe = true;
    } else {
        g_dev_mgr.thread_safe = false;
    }
}

#if 0
// Example device operation implementation
int sample_init(void *priv) {
    printf("Device initialized\n");
    return 0;
}

int sample_open(void *priv) {
    printf("Device opened\n");
    return 0;
}

int sample_ioctl(void *priv, unsigned int cmd, unsigned long arg) {
    printf("IOCTL command: %u, arg: %lu\n", cmd, arg);
    return 0;
}

// Example lock function
void sample_lock(void) {
    // In actual project this might be pthread_mutex_lock, etc.
    printf("Lock acquired\n");
}

void sample_unlock(void) {
    // In actual project this might be pthread_mutex_unlock, etc.
    printf("Lock released\n");
}

// Callback function to print device information
int print_device_info(device_t *dev, void *arg) {
    const char *type_str[] = {"Char", "Block", "Net"};
    printf("Device: %-15s Type: %s\n", dev->name, 
           dev->type < DEV_TYPE_MAX ? type_str[dev->type] : "Unknown");
    return 0;
}

int main(void)
{
    // Initialize device manager and pass lock functions
    device_manager_init(sample_lock, sample_unlock);
    
    // Create device instances
    device_t *dev1 = hal_mem_alloc_fast(sizeof(device_t));
    strcpy(dev1->name, "sensor0");
    dev1->type = DEV_TYPE_CHAR;
    dev1->ops = &(dev_ops_t){.init = sample_init, .open = sample_open, .ioctl = sample_ioctl};
    dev1->priv_data = NULL;
    
    device_t *dev2 = hal_mem_alloc_fast(sizeof(device_t));
    strcpy(dev2->name, "disk0");
    dev2->type = DEV_TYPE_BLOCK;
    dev2->ops = &(dev_ops_t){.init = sample_init, .open = sample_open};
    dev2->priv_data = NULL;
    
    device_t *dev3 = hal_mem_alloc_fast(sizeof(device_t));
    strcpy(dev3->name, "eth0");
    dev3->type = DEV_TYPE_NET;
    dev3->ops = &(dev_ops_t){.init = sample_init, .start = sample_open};
    dev3->priv_data = NULL;
    
    // Register devices
    device_register(dev1);
    device_register(dev2);
    device_register(dev3);
    
    // Use new API
    printf("\nAll devices:\n");
    device_foreach(print_device_info, NULL);
    
    printf("\nBlock devices:\n");
    device_foreach_type(DEV_TYPE_BLOCK, print_device_info, NULL);
    
    printf("\nTotal devices: %d\n", device_count(-1));
    
    // Find device
    device_t *found = device_find_pattern("eth", -1);
    if (found) {
        printf("\nFound device: %s\n", found->name);
    }
    
    // Unregister devices
    device_unregister(dev1);
    device_unregister(dev2);
    device_unregister(dev3);
    hal_mem_free(dev1);
    hal_mem_free(dev2);
    hal_mem_free(dev3);
    
    return 0;
}
#endif