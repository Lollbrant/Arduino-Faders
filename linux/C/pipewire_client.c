#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pipewire/pipewire.h>
#include <pipewire/keys.h>
#include <pipewire/core.h>
#include <pipewire/node.h>
#include <spa/param/audio/format.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/utils/result.h>
#include <spa/pod/vararg.h>
#include <spa/pod/builder.h>
#include "cJSON.h"
#include "uthash.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <poll.h>

#define DEFAULT_CONFIG_PATH "/etc/HyperControl/HyperFader.json"
#define MAX_IDS 16
#define SERIAL_BUFFER_SIZE 1024
#define MAX_FADERS 8

typedef struct {
    int id;
    char class[64];
    char name[64];
    char *dev_id;
    char *dev_version;
    UT_hash_handle hh;
} FaderMap;

struct pwPortList {
    struct pw_port *port;
    char port_name[64];
    UT_hash_handle hh;
};

struct pwNodesIDList {
    uint32_t ID;
    struct pw_node *node;
    struct pwPortList *portList;
    uint32_t channels;
    struct spa_hook listener;
    UT_hash_handle hh;
};
struct pwNodeMap {
    char name[64];
    char class[64];
    size_t count;
    struct pwNodesIDList *id_list;
    UT_hash_handle hh;
};

FaderMap *faderConfig = NULL;
struct pwNodeMap *pwNodes = NULL;

int baud_rate = 0;
char *serialPort = NULL;
char *log_level = NULL;
char *device_id = NULL;
int last_fader_value[MAX_FADERS];

struct pw_registry *registry = NULL;

void load_config(const char *path){
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Could not open config file: %s\n", path);
        return;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json_data = malloc(len + 1);
    fread(json_data, 1, len, f);
    json_data[len] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(json_data);
    if (!root) {
        fprintf(stderr, "Error before: %s\n", cJSON_GetErrorPtr());
        free(json_data);
        return;
    }

    if (cJSON_GetObjectItem(root, "port")->valuestring == NULL) {
        fprintf(stderr, "No serial port defined!");
        free(json_data);
        return;
    }
    else {
        serialPort = strdup(cJSON_GetObjectItem(root, "port")->valuestring);
    }

    if (cJSON_GetObjectItem(root, "log_level")->valuestring == NULL) {
        log_level = "info";
    }
    else {
        log_level = strdup(cJSON_GetObjectItem(root, "log_level")->valuestring);
    }

    baud_rate = cJSON_GetObjectItem(root, "baud_rate")->valueint;

    cJSON *devices = cJSON_GetObjectItem(root, "devices");
    if (!cJSON_IsArray(devices)) {
        fprintf(stderr, "Devices is not an array\n");
        cJSON_Delete(root);
        free(json_data);
        return;
    }
    cJSON *device_obj = NULL;
    cJSON_ArrayForEach(device_obj, devices) {
        cJSON *device_id_json = device_obj->child;
        if (device_id_json) {
            //device_id = device_id_json->string;

            cJSON *faders = device_id_json;
            if (faders && cJSON_IsArray(faders)) {
                cJSON *fader = NULL;
                cJSON_ArrayForEach(fader, faders) {
                    int id = cJSON_GetObjectItem(fader, "id")->valueint;
                    const char *class = cJSON_GetObjectItem(fader, "class")->valuestring;
                    const char *name = cJSON_GetObjectItem(fader, "name")->valuestring;

                    FaderMap *entry = malloc(sizeof(FaderMap));
                    entry->id = id;
                    strncpy(entry->class, class, sizeof(entry->class)-1);
                    strncpy(entry->name, name, sizeof(entry->name)-1);

                    HASH_ADD_INT(faderConfig, id, entry);
                }
            }
        }
    }
    cJSON_Delete(root);
    free(json_data);
}

int open_serial(const char *device, int baud_rate) {

    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK | O_SYNC);
    speed_t terminal_speed = B0;

    if (fd < 0) {
        fprintf(stderr, "Could not open: %s\n", device);
        return -1;
    }
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "Could not read: %s\n", device);
        close(fd);
        return -1;
    }
    cfmakeraw(&tty);

    // Support for multiple baud rates but default to 115200 baud
    switch (baud_rate) {
        case 9600:
            terminal_speed = B9600;
            break;
        case 38400:
            terminal_speed = B38400;
            break;
        default:
              terminal_speed = B115200;
              break;
    }

    cfsetospeed(&tty, terminal_speed);
    cfsetispeed(&tty, terminal_speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
    tty.c_iflag &= ~IGNBRK;                         // disable break processing
    tty.c_lflag = 0;                                // no signaling chars, no echo
    tty.c_oflag = 0;                                // no remapping, no delays
    tty.c_cc[VMIN]  = 1;                            // read at least 1 char
    tty.c_cc[VTIME] = 0;                            // no timeout

    tty.c_cflag |= (CLOCAL | CREAD);                // ignore modem controls, enable reading
    tty.c_cflag &= ~(PARENB | PARODD);              // shut off parity
    tty.c_cflag &= ~CSTOPB;                         // 1 stop bit
    tty.c_cflag &= ~CRTSCTS;                        // no flow control

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "Could not access: %s\n", device);
        close(fd);
        return -1;
    }
    return fd;
}

struct pwNodeMap *get_pw_node_for_fader(int fid) {
    FaderMap *faderfound = NULL;
    struct pwNodeMap *entry, *tmp;
    HASH_FIND_INT(faderConfig, &fid, faderfound);
    if (!faderfound) {
        fprintf(stderr, "Fader %d not found\n", fid);
        return NULL;
    }
    HASH_ITER(hh, pwNodes, entry, tmp) {
        if (strstr(entry->name, faderfound->name)) {
            return entry;
        }
    }
    return entry;
}

float scale_fader_value(int raw_value, int min, int max) {
    if (raw_value < min) { raw_value = min; }
    if (raw_value > max) { raw_value = max; }
    return (float)(raw_value - min) / (float)(max - min);
}

void set_node_volume(struct pwNodeMap *node, float volume) {
    if (!node) {
        fprintf(stderr, "set_node_volume: NULL node pointer!\n");
        return;
    }

    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(NULL, 0);
    uint8_t buffer[1024];
    spa_pod_builder_init(&b, buffer, sizeof(buffer));

    struct pwNodesIDList *id_entry;
    for (id_entry = node->id_list; id_entry != NULL; id_entry = (struct pwNodesIDList*)(id_entry->hh.next)) {

        //int channels = id_entry->channels;

        //if (channels <= 0 || channels > 64) {
        //        fprintf(stderr, "Invalid channel count: %d, skipping node (%d)\n", channels, id_entry->ID);
        //}
        //float *volumes = calloc(channels, sizeof(float));
        //for (int i = 0; i < channels; i++) {
        //    volumes[i] = volume;
        //}

        const struct spa_pod *param = spa_pod_builder_add_object(&b,
                SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
                SPA_PROP_volume, SPA_POD_Float(volume)
        );
            pw_node_set_param((struct pw_node *)id_entry->node, SPA_PARAM_Props, 0, param);
    }
}


void parse_serial_json(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    int min_value = 0;
    int max_value = 0;
    int fader_count = 0;
    //char *dev_id;
    //char *dev_version;
    if (!root) {
        fprintf(stderr, "JSON Parse Error: %s\n", cJSON_GetErrorPtr());
        return;
    }

    //// Extract "id"
    //cJSON *id = cJSON_GetObjectItem(root, "id");
    //if (cJSON_IsString(id)) {
    //    dev_id = id->valuestring;
    //}

    //// Extract "version"
    //cJSON *version = cJSON_GetObjectItem(root, "version");
    //if (cJSON_IsString(version)) {
    //    dev_version = version->valuestring;
    //}

    // Extract "spec"
    cJSON *spec = cJSON_GetObjectItem(root, "spec");
    if (cJSON_IsObject(spec)) {
        if (cJSON_IsNumber(cJSON_GetObjectItem(spec, "faderCount"))) {
            fader_count = cJSON_GetObjectItem(spec, "faderCount")->valueint;
        }
        if (cJSON_IsNumber(cJSON_GetObjectItem(spec, "min"))) {
            min_value = cJSON_GetObjectItem(spec, "min")->valueint;
        }
        if (cJSON_IsNumber(cJSON_GetObjectItem(spec, "max"))) {
            max_value = cJSON_GetObjectItem(spec, "max")->valueint;
        }
    }

    // Extract "faders"
    cJSON *faders = cJSON_GetObjectItem(root, "faders");
    if (cJSON_IsArray(faders)) {
        //int fader_count = cJSON_GetArraySize(faders);
        for (int i = 0; i < fader_count; i++) {
            cJSON *fader = cJSON_GetArrayItem(faders, i);
            if (cJSON_IsObject(fader)) {
                cJSON *fid = cJSON_GetObjectItem(fader, "id");
                cJSON *val = cJSON_GetObjectItem(fader, "value");
                if (cJSON_IsNumber(fid) && cJSON_IsNumber(val)) {
                    int fader_id = fid->valueint;
                    int value = val->valueint;
                    struct pwNodeMap *entry = get_pw_node_for_fader(fader_id);
                    if (!entry) {
                        continue;
                    }
                    if (abs(value - last_fader_value[fader_id]) > 3) {
                        float vol = scale_fader_value(value, min_value, max_value);
                        //fprintf(stdout, "Fader: %d sets the volume of %s to %.2f\n", fader_id, entry->name, vol);
                        set_node_volume(entry, vol);
                        last_fader_value[fader_id] = value;
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
}

void handle_serial(int fd) {
    static char buffer[SERIAL_BUFFER_SIZE];
    static int pos = 0;
    char tmp[128];
    int n = read(fd, tmp, sizeof(tmp));  // Read as much as is available
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            char c = tmp[i];
            if (c == '\n') {
                buffer[pos] = '\0';
                parse_serial_json(buffer);
                pos = 0;  // Reset position for next line
            } else if (pos < SERIAL_BUFFER_SIZE - 1) {
                buffer[pos++] = c;
            } else {
                fprintf(stderr, "Serial buffer overflow\n");
                pos = 0;
            }
        }
    } else if (n < 0 && errno != EAGAIN) {
        fprintf(stderr, "Serial read failed");
    }
}




static void on_node_param(void *data, int seq, uint32_t id, uint32_t index,
        uint32_t next, const struct spa_pod *param) {
    struct pwNodesIDList *entry = data;
    if (id == SPA_PARAM_Format && param) {
        struct spa_audio_info_raw info = { 0 };
        spa_format_audio_raw_parse(param, &info);
        entry->channels = info.channels;
    }
}

static const struct pw_node_events node_events = {
    PW_VERSION_NODE_EVENTS,
    .param = on_node_param,
};

void setup_node_listener(struct pwNodesIDList *entry) {
    pw_node_add_listener(entry->node, &entry->listener, &node_events, entry);
    pw_node_enum_params(entry->node, 0, SPA_PARAM_Format, 0, 1, NULL);
}

static void on_pipewire_global(void *data, uint32_t id, uint32_t permissions,
                      const char *type, uint32_t version,
                      const struct spa_dict *props) {

    const char *media_class = NULL;
    const char *node_name = NULL;
    const char *node_id_str = NULL;
    const char *app_name = NULL;
    const char *port_name = NULL;
    struct pw_node *node = NULL;


    if (props) {
        media_class = spa_dict_lookup(props, "media.class");
        node_name = spa_dict_lookup(props, "node.name");
        node_id_str = spa_dict_lookup(props, "node.id");
        app_name = spa_dict_lookup(props, "application.name");
        port_name = spa_dict_lookup(props, "port.name");
    }

    if (strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
        if (strcmp(spa_dict_lookup(props, "port.direction"), "in") == 0) {
            int node_id = atoi(node_id_str);
            struct pwNodeMap *map_entry, *tmp;
            HASH_ITER(hh, pwNodes, map_entry, tmp) {
                struct pwNodesIDList *id_entry = NULL;
                HASH_FIND(hh, map_entry->id_list, &node_id, sizeof(uint32_t), id_entry);
                if (id_entry) {
                    struct pwPortList *port_entry = NULL;
                    HASH_FIND_STR(id_entry->portList, port_name, port_entry);
                    if (!port_entry) {
                        port_entry = calloc(1, sizeof(struct pwPortList));
                        port_entry->port = pw_registry_bind(registry, id, PW_TYPE_INTERFACE_Port, version, 0);
                        strncpy(port_entry->port_name, port_name, sizeof(port_entry->port_name)-1);
                        HASH_ADD_STR(id_entry->portList, port_name, port_entry);
                    }
                }
            }
        }
    }

    if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        if (media_class && node_name) {
            fprintf(stdout, "Found new controllable node: [application_name: %s] [media_class: %s] [node_name: %s] [node_id %u]\n",
                app_name ? app_name : "(null)",
                media_class ? media_class : "(null)",
                node_name ? node_name : "(null)",
                id);

            struct pwNodeMap *map_entry = NULL;
            HASH_FIND_STR(pwNodes, node_name, map_entry);
            if (!map_entry) {
                map_entry = malloc(sizeof(struct pwNodeMap));
                strncpy(map_entry->name, node_name, sizeof(map_entry->name)-1);
                strncpy(map_entry->class, media_class, sizeof(map_entry->class)-1);
                map_entry->id_list = NULL;
                map_entry->count = 0;
                HASH_ADD_STR(pwNodes, name, map_entry);
            }

            struct pwNodesIDList *id_entry = NULL;
            HASH_FIND(hh, map_entry->id_list, &id, sizeof(uint32_t), id_entry);

            if (!id_entry) {
                id_entry = calloc(1, sizeof(struct pwNodesIDList));

                node = pw_registry_bind(registry, id, PW_TYPE_INTERFACE_Node, version, 0);

                id_entry->ID = id;
                id_entry->node = node;

                setup_node_listener(id_entry);
                HASH_ADD(hh, map_entry->id_list, ID, sizeof(uint32_t), id_entry);
                map_entry->count++;
            }
        }
    }
}
static void on_pipewire_remove(void *data, uint32_t id) {
    struct pwNodeMap *map_entry, *tmp;
    HASH_ITER(hh, pwNodes, map_entry, tmp) {
        struct pwNodesIDList *id_entry = NULL;
        HASH_FIND(hh, map_entry->id_list, &id, sizeof(uint32_t), id_entry);
        if (id_entry) {
            fprintf(stdout, "node_id: %d was removed!\n", id_entry->ID);
            HASH_DEL(map_entry->id_list, id_entry);
            free(id_entry);
            map_entry->count--;
        }
        if (map_entry->count == 0) {
            fprintf(stdout, "%s was removed!\n", map_entry->name);
            HASH_DEL(pwNodes, map_entry);
            free(map_entry);
        }
    }
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = on_pipewire_global,
    .global_remove = on_pipewire_remove,
};


int main(int argc, char *argv[]) {
    load_config(DEFAULT_CONFIG_PATH);

    pw_init(&argc, &argv);
    struct pw_main_loop *pwMainLoop = pw_main_loop_new(NULL);
    struct pw_loop *pwLoop = pw_main_loop_get_loop(pwMainLoop);
    struct pw_context *context = pw_context_new(pwLoop, NULL, 0);
    struct pw_core *PWcore = pw_context_connect(context, NULL, 0);
    memset(last_fader_value, 0, sizeof(last_fader_value));
    if (!PWcore) {
        fprintf(stderr, "Failed to connect to PipeWire\n");
        return -1;
    }
    registry = pw_core_get_registry(PWcore, PW_VERSION_REGISTRY, 0);
    struct spa_hook registry_listener;
    pw_registry_add_listener(registry, &registry_listener, &registry_events, NULL);

    int pw_fd = pw_loop_get_fd(pwLoop);
    int serial_fd = open_serial(serialPort, baud_rate);

    struct pollfd fds[2];
    fds[0].fd = serial_fd;
    fds[0].events = POLLIN;
    fds[1].fd = pw_fd;
    fds[1].events = POLLIN;

    while (1) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            fprintf(stderr, "Could not poll fd");
            break;
        }

        if (fds[0].revents & POLLIN) {
            handle_serial(serial_fd);
        }
        if (fds[1].revents & POLLIN) {
            pw_loop_iterate(pwLoop, 0);
        }
    }

    close(serial_fd);
    pw_main_loop_destroy(pwMainLoop);
    pw_deinit();

    FaderMap *current, *tmp;

    HASH_ITER(hh, faderConfig, current, tmp) {
        HASH_DEL(faderConfig, current);
        free(current);
    }
    free(tmp);

    return 0;
}
