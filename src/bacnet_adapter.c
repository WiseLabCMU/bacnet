/******************************************************************************
 *  Mortar IO (MIO) Library
 *  C Strophe Implementation
 *  Copyright (C) 2014, Carnegie Mellon University
 *  All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.0 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Contributing Authors (specific to this file):
 *  Christopher Palmer
 *******************************************************************************/

#define PRINT_ENABLED 1

#include "adapter.h"
#include "xml_tools.h"

/* MIO includes */
#include <mio_handlers.h>
#include <mio.h>
#include <strophe.h>

/* library include */
#include "uthash.h"
#include "expat.h"

/* BACnet include */
#include "bactext.h"
#include "bacerror.h"
#include "iam.h"
#include "arf.h"
#include "tsm.h"
#include "address.h"
#include "npdu.h"
#include "apdu.h"
#include "device.h"
#include "net.h"
#include "datalink.h"
#include "whois.h"
#include "wp.h"
#include "rp.h"
#include "handlers.h"
#include "client.h"
#include "txbuf.h"
#include "dlenv.h"

#include "bacdef.h"
#include "config.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>       /* for time */
#include <string.h>
#include <ctype.h>      /* toupper */

#include <errno.h>
#include <dirent.h>


#define TRUE 1

#ifndef MAX_PROPERTY_VALUES
#define MAX_PROPERTY_VALUES 64
#endif


/** Bacnet representation of device along with MIO
 * mappings. */
typedef struct bacnet_device {
    char *node_id; /** Event node id associated with device*/
    int32_t instance;
    char* type;
    int32_t n_objects;
    int32_t *obj_ids;
    int32_t *obj_types;
    char **obj_names;
    UT_hash_handle nodemap; /** map from node to bacnet_device */
    UT_hash_handle instancemap; /** map from objid to bacnet_device */
} bacnet_device_t;

/** Stores bacnet context information along with device and instance map */
typedef struct bacnet_context {
    BACNET_ADDRESS src; /** local bacnet address */
    int32_t port; /** port for BACNET to run on */
    bacnet_device_t *node_devices; /** map from node to bacnet_device*/
    bacnet_device_t *instance_devices;
} bacnet_context_t;

/********************* static parser variables **********************************/
static bacnet_context_t *parser_context = NULL;
static bacnet_device_t *parser_device = NULL;
static int parser_depth = 0;
static int parser_device_instance;
static char *parser_device_type = NULL;
static int parser_object_instance = 0;
static int parser_object_type = 0;
static char *parser_object_name = NULL;
static char* parser_node_id = NULL;
static pthread_mutex_t *mio_lock;
/* End static parser variables */

/********************* static BACnet variables *******************/
static char value[20];
static int value_length = 20;

static BACNET_ADDRESS target_address;
static int found = 0;
static int error_detected = 0;
static uint8_t request_invoke_id = 0;
static int write_acked = 0;
/* End bacnet static variables */

static void bacnet_start(void *data, const char *element, const char **attribute);
static void bacnet_end(void *data, const char *el);
static void device_start(void *data, const char *element, const char **attribute);
static void device_end(void *data, const char *el);
static bacnet_context_t* parse_bacnet_context_node(adapter_t *adapter);
int write_bacnet_obj(bacnet_device_t *device, int obj_index, char *value);
static void Init_Service_Handlers(void);
int publish_data(adapter_t *adapter);
int bacnet_connect ();

bacnet_context_t* new_bacnet_context()
{
    bacnet_context_t *context = (bacnet_context_t*) malloc(sizeof(bacnet_context_t));
    memset(context,0x0,sizeof(bacnet_context_t));
    return context;
}

bacnet_device_t* new_bacnet_device()
{
    bacnet_device_t *device = (bacnet_device_t*) malloc(sizeof(bacnet_device_t));
    memset(device,0x0,sizeof(bacnet_context_t));
    return device;
}

/********************* Public bacnet_adapter definitions **************/

void* bacnet_parser(void* adapterv)
{
    adapter_t *adapter = (adapter_t*) adapterv;
    char* config_dir = adapter -> config_dir;
    bacnet_context_t* context;
    parser_context = new_bacnet_context();

    char bacnet_file[512];
    char device_file[512];

    DIR *d;
    d = opendir(config_dir);

    if (d != NULL)
    {
        sprintf(device_file,"%s/device.xml",(char*)config_dir);
        parse_xml(device_file, device_start,device_end);

        sprintf(bacnet_file,"%s/bacnet.xml",(char*)config_dir);
        parse_xml(bacnet_file, bacnet_start, bacnet_end);

        mio_lock = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(mio_lock,NULL);

        context = parser_context;
        parser_context = NULL;
    } else
    {
        context = parse_bacnet_context_node(adapter);
    }

    address_init();
    Init_Service_Handlers();
    dlenv_init();
    return (void*) context;
}


void* bacnet_actuate(void* vadapter)
{
    adapter_t *adapter = (adapter_t*) vadapter;
    bacnet_context_t *context = (bacnet_context_t*) (adapter -> context);
    mio_conn_t* pubsub_conn = adapter -> connection;
    mio_packet_t* packet;
    mio_data_t* data;
    mio_response_t* response;
    mio_transducer_value_t* tran;
    int32_t err;

    bacnet_device_t *device;

    while (TRUE) {
        if (pubsub_conn -> xmpp_conn -> state == XMPP_STATE_CONNECTED) {
            response = mio_response_new();
            err = mio_pubsub_data_receive(pubsub_conn, response);
            if (err != MIO_OK)
            {
                printf("mio_pubsub_data_receive failed with error: %d\n", err);
                continue;
            }
            pthread_mutex_lock(mio_lock);
            packet = (mio_packet_t*) response->response;
            data = (mio_data_t*) packet->payload;
            for (tran = data->transducers; tran != NULL; tran = tran->next) {
                if (tran -> type != MIO_TRANSDUCER_SET_VALUE)
                    continue;
                else
                {
                    for (device = context -> node_devices;
                            device; device = device -> nodemap.next)
                    {
                        if (strcmp(device -> node_id,data->event) !=0)
                            continue;
                        int i;
                        for (i = 0; i < device -> n_objects; i++)
                        {
                            if (strcmp(device -> obj_names[i],tran -> name) == 0)
                            {
                                break;
                            }
                        }
                        if (i == device -> n_objects)
                        {
                            continue;
                        }
                        err = write_bacnet_obj(device, i, tran -> typed_value);
                        if (err)
                            printf("error with writing bacnet object\n");
                    }
                }

            }
            pthread_mutex_unlock(mio_lock);
            mio_response_free(response);
        } else
            usleep(1000);

    }
}

void* bacnet_data(void* adapter)
{
    pthread_mutex_lock(mio_lock);
    while(TRUE)
    {
        publish_data(adapter);
        pthread_mutex_unlock(mio_lock);
        sleep(10);
        pthread_mutex_lock(mio_lock);
    }
    return NULL;
}

/** End public definitions */


/********************* Mio functions ************/

int publish_data(adapter_t *adapter)
{
    static uint8_t rx_buf[MAX_MPDU] = {0};
    bacnet_context_t *context = (bacnet_context_t*) (adapter -> context);
    bacnet_device_t *device;
    time_t current_seconds, elapsed_seconds = 0;
    int pdu_len;
    char *time_str, *node_id;
    time_t last_seconds,timeout_seconds;
    unsigned timeout = 100;
    BACNET_ADDRESS  src = { 0 };
    unsigned max_apdu = 0;
    mio_stanza_t *item;
    mio_response_t *publish_response;
    for (device = context -> node_devices; device; device = device -> nodemap.next)
    {
        time_str = mio_timestamp_create();
        item = mio_pubsub_item_data_new(adapter -> connection);
        node_id = device -> node_id;

        last_seconds = time(NULL);
        timeout_seconds = (apdu_timeout() / 1000) * apdu_retries();
        //if (!found)
        found =
            address_bind_request(device -> instance, &max_apdu,
                                 &target_address);
        if (!found)
        {
            Send_WhoIs(device -> instance, device -> instance);
        }
        while (!found)
        {
            current_seconds = time(NULL);
            if (current_seconds != last_seconds)
                tsm_timer_milliseconds((uint16_t) ((current_seconds -
                                                    last_seconds) * 1000));
            if (!found)
            {
                found =
                    address_bind_request(device -> instance,
                                         &max_apdu,&target_address);
            }
            if (!found)
            {
                elapsed_seconds += (current_seconds - last_seconds);
                if (elapsed_seconds > timeout_seconds) {
                    printf("\rError: APDU Timeout!\r\n");
                    return 1;
                }
            }
            pdu_len = datalink_receive(&src, &rx_buf[0], MAX_MPDU,timeout);
            if (pdu_len) {
                npdu_handler(&src, &rx_buf[0], pdu_len);
            }
            last_seconds = current_seconds;
        }
        int i;
        printf("n_objects %d\n", device -> n_objects);
        for ( i = 0; i < device -> n_objects; i++)
        {
            request_invoke_id = 0;
            while(TRUE)
            {
                printf("true\n");
                if (found)
                {
                    if (request_invoke_id == 0)
                    {
                        request_invoke_id =
                            Send_Read_Property_Request(
                                device->instance, device -> obj_types[i],
                                device -> obj_ids[i], 85,
                                -1);
                    } else if (tsm_invoke_id_free(request_invoke_id))
                        break;
                    else if (tsm_invoke_id_failed(request_invoke_id))
                    {
                        fprintf(stderr,"\rError: TSM Timeout!\r\n");
                        tsm_free_invoke_id(request_invoke_id);
                        return 1;
                    }
                }
                pdu_len = datalink_receive(&src, &rx_buf[0], MAX_MPDU,timeout);
                if (pdu_len) {
                    npdu_handler(&src, &rx_buf[0], pdu_len);
                }
                last_seconds = current_seconds;
            }
            if (error_detected)
            {
                error_detected = 0;
                return 1;
            } else
            {
                if (strcmp(value,"active") == 0)
                    mio_item_transducer_value_add(item, NULL, device -> obj_names[i] , "1", value,  time_str);
                else if (strcmp(value,"inactive") == 0)
                    mio_item_transducer_value_add(item, NULL, device -> obj_names[i] , "0",value,  time_str);
                else
                    mio_item_transducer_value_add(item, NULL, device -> obj_names[i] , value, value, time_str);
            }

        }
        publish_response = mio_response_new();
        while (adapter -> connection -> xmpp_conn -> state != XMPP_STATE_CONNECTED)
            usleep(1000);
        mio_item_publish_data(adapter -> connection,item,node_id,publish_response);
        free(time_str);
        mio_response_print(publish_response);
        mio_response_free(publish_response);
        mio_stanza_free(item);
    }
    return 0;
}


/**************************** Parsing definitions ************************/

static bacnet_context_t* parse_bacnet_context_node(adapter_t *adapter)
{
    mio_response_t *bacnet_ref_response;
    mio_response_t *bacnet_meta_response;
    mio_reference_t *bacnet_ref;
    mio_reference_t *ref_tmp;
    int err, count;
    int bacnet_id;
    char *parser_object_type, *bacnet_name;
    bacnet_device_t *devices;
    bacnet_device_t *device;
    mio_meta_t *meta;
    mio_transducer_meta_t *transducer;
    mio_property_meta_t *property;

    char* node_id;

    bacnet_context_t *b_context = malloc(sizeof(bacnet_context_t));
    memset(b_context, 0x0, sizeof(bacnet_context_t));

    // get children
    bacnet_ref_response = mio_response_new();
    err = mio_references_query(adapter -> connection, adapter -> config_dir, bacnet_ref_response);
    if (err != MIO_OK)
    {
        printf("Could not retrieve any children.\n");
        return NULL;
    }

    // iterate through bacnet_children
    mio_packet_t *packet = (mio_packet_t*) bacnet_ref_response -> response;
    bacnet_ref = (mio_reference_t*) packet -> payload;

    for (ref_tmp = bacnet_ref; ref_tmp != NULL; ref_tmp = ref_tmp -> next)
    {
        if (ref_tmp -> type != MIO_REFERENCE_CHILD)
        {
            continue;
        }

        node_id = ref_tmp -> node;
        device = new_bacnet_device();
        device -> node_id = parser_node_id;

        bacnet_meta_response = mio_response_new();
        err = mio_meta_query(adapter -> connection, node_id, bacnet_meta_response );
        if (err != MIO_OK)
        {
            continue;
        }

        packet = (mio_packet_t*) bacnet_meta_response -> response;
        meta = (mio_meta_t*) packet -> payload;
        // iterate over meta properties
        for (property = meta -> properties; property != NULL; property = property -> next)
        {
            if (strcmp(property -> name, "BACnet Type") == 0)
            {
                device -> type = string_copy(property -> value);
            } else if (strcmp(property -> name, "BACnet Instance") == 0)
            {
                device -> instance = atoi( property-> value);
            } /*else if (strcmp(property -> name, "BACnet IP Address") == 0)
           {
           }*/
        }

        // iterate over transducers
        count = 0;
        for (transducer = meta -> transducers; transducer != NULL; transducer = transducer -> next)
            count++;
        device -> obj_ids = (int*) malloc(count * sizeof(int));
        device -> obj_types = (int*) malloc(count * sizeof(int));
        device -> obj_names = (char**) malloc(count * sizeof(char*));

        for (transducer = meta -> transducers; transducer != NULL; transducer = transducer -> next)
        {
            // iterate over transducer properties
            //
            count = 0;
            for (property = transducer -> properties; property != NULL; property = property -> next)
            {
                if (strcmp(property -> name, "BACnet Type") == 0)
                {
                    if (strcmp(property -> value,"AV") == 0)
                    {
                        device -> obj_types[count] = OBJECT_ANALOG_VALUE;
                    } else if (strcmp(property -> value,"BV") == 0)
                    {
                        device -> obj_types[count] = OBJECT_BINARY_VALUE;
                    }
                } else if (strcmp(property -> name, "BACnet Name") == 0)
                {
                    device -> obj_names[count] = string_copy(property -> value);
                } else if (strcmp(property -> name, "BACnet ID") == 0)
                {
                    device -> obj_ids[count] = atoi(property -> value);
                }
                count ++;
            }
            HASH_ADD(nodemap, b_context -> instance_devices, instance, sizeof(int32_t),device);
            HASH_ADD(instancemap, b_context -> node_devices, instance,strlen(device -> node_id),device);
        }
        mio_response_free(bacnet_meta_response);
    }
    return b_context;
}

static void device_start(void *data, const char *element, const char **attribute)
{
    int32_t i;
    parser_depth++;
    for (i = 0; attribute[i]; i += 1)
    {
        if (parser_depth == 2)
        {
            if (strcmp("type",attribute[i]) == 0)
            {
                parser_device_type = string_copy(attribute[i+1]);
                i++;
            }

        } else if (parser_depth == 3)
        {

            if (strcmp(attribute[i],"bacnet_type") == 0)
            {
                if (strcmp(attribute[i+1],"AV") == 0)
                {
                    parser_object_type = OBJECT_ANALOG_VALUE;
                } else if (strcmp(attribute[i+1],"BV") == 0)
                {
                    parser_object_type = OBJECT_BINARY_VALUE;
                }
                i++;
            } else if (strcmp(attribute[i],"bacnet_name") == 0)
            {
                parser_object_name = string_copy(attribute[i+1]);
                i++;
            } else if (strcmp(attribute[i],"bacnet_id") == 0)
            {
                parser_object_instance = atoi(attribute[i+1]);
                i++;
            }

        }
    }
}

static void device_end(void *data, const char *el)
{
    if (parser_device == NULL)
        parser_device = new_bacnet_device();
    if (parser_depth == 2)
    {
        parser_device -> type = parser_device_type;
    } else if (parser_depth == 3)
    {
        parser_device -> n_objects++;
        int *tmp_obj_ids = (int*) malloc(parser_device -> n_objects * sizeof(int));
        if (parser_device ->n_objects != 1)
        {
            memcpy(tmp_obj_ids, parser_device -> obj_ids,sizeof(int)*(parser_device -> n_objects - 1));
            free(parser_device -> obj_ids);
        }
        parser_device -> obj_ids = tmp_obj_ids;
        parser_device -> obj_ids[parser_device -> n_objects - 1] = parser_object_instance;

        int *tmp_obj_types = (int*) malloc(parser_device -> n_objects * sizeof(int));
        if (parser_device ->n_objects != 1)
        {
            memcpy(tmp_obj_types, parser_device -> obj_types,sizeof(int)*(parser_device -> n_objects - 1));
            free(parser_device -> obj_types);
        }
        parser_device -> obj_types = tmp_obj_types;
        parser_device -> obj_types[parser_device -> n_objects - 1] = parser_object_type;

        char **tmp_obj_names = (char**) malloc(parser_device -> n_objects * sizeof(char*));
        if (parser_device ->n_objects != 1)
        {
            memcpy(tmp_obj_names,  parser_device -> obj_names,sizeof(char*)*(parser_device -> n_objects - 1));
            free(parser_device -> obj_names);
        }
        parser_device -> obj_names = tmp_obj_names;
        parser_device -> obj_names[parser_device -> n_objects - 1] = parser_object_name;

    }
    parser_depth--;
}
static void bacnet_start(void *data, const char *element, const char **attribute)
{
    int32_t i;
    parser_depth++;
    for (i = 0; attribute[i]; i += 1)
    {
        if (parser_depth == 1)
            i++;
        if (parser_depth == 2)
        {
            if(strcmp("type",attribute[i]) == 0)
            {
                parser_device_type = string_copy(attribute[i+1]);
                i++;
            } else if (strcmp("instance",attribute[i]) == 0)
            {
                parser_device_instance = atoi(attribute[i+1]);
                i++;
            } else if (strcmp("node_id",attribute[i]) == 0)
            {
                parser_node_id = string_copy(attribute[i+1]);
                i++;
            } else
                i++;
        }
    }
}

static void bacnet_end(void *data, const char *el)
{

    bacnet_device_t *tmp_device;
    int n_objs, i;
    if (parser_depth == 2)
    {
        // copy
        tmp_device = new_bacnet_device();
        tmp_device -> node_id = parser_node_id;
        parser_node_id = NULL;
        tmp_device -> type = parser_device_type;
        parser_device_type = NULL;
        tmp_device -> instance = parser_device_instance;
        parser_device_instance = 0;
        n_objs = parser_device -> n_objects;
        tmp_device -> n_objects = n_objs;
        tmp_device -> obj_ids = (int*) malloc(n_objs * sizeof(int));
        tmp_device -> obj_types = (int*) malloc(n_objs * sizeof(int));
        tmp_device -> obj_names = (char**) malloc(n_objs * sizeof(char*));
        for (i = 0; i < n_objs; i++)
        {
            tmp_device -> obj_ids[i] = parser_device -> obj_ids[i];
            tmp_device -> obj_types[i] = parser_device -> obj_types[i];
            tmp_device -> obj_names[i] = string_copy(parser_device -> obj_names[i]);
        }
        HASH_ADD(nodemap, parser_context->instance_devices,instance, sizeof(int32_t),tmp_device);
        HASH_ADD(instancemap, parser_context->node_devices,instance,strlen(tmp_device->node_id),tmp_device);
    }
    parser_depth--;
}

/* END parsing definitions */

/******************* BACNET service functions **************************/


/*int bacnet_connect ()
{
    last_seconds = time(NULL);
    timeout_seconds = (apdu_timeout() / 1000) * apdu_retries();
            //if (!found)
    found =
    address_bind_request(device -> instance, &max_apdu,
       &target_address);
    if (!found)
    {
        Send_WhoIs(device -> instance, device -> instance);
    }
    while (!found)
    {
        current_seconds = time(NULL);
        if (current_seconds != last_seconds)
            tsm_timer_milliseconds((uint16_t) ((current_seconds -
                last_seconds) * 1000));
        if (!found)
        {
            found =
            address_bind_request(device -> instance,
               &max_apdu,&target_address);
        }
        if (!found)
        {
            elapsed_seconds += (current_seconds - last_seconds);
            if (elapsed_seconds > timeout_seconds) {
                printf("\rError: APDU Timeout!\r\n");
                return 1;
            }
        }
        pdu_len = datalink_receive(&src, &rx_buf[0], MAX_MPDU,timeout);
        if (pdu_len) {
            npdu_handler(&src, &rx_buf[0], pdu_len);
        }
        last_seconds = current_seconds;
        }
    }
}*/

int write_bacnet_obj(bacnet_device_t *device, int obj_index, char *value)
{
    uint8_t rx_buf[MAX_MPDU] = {0};
    BACNET_ADDRESS src = {
        0
    };  /* address where message came from */
    uint16_t pdu_len = 0;
    unsigned timeout = 500;     /* milliseconds */
    unsigned max_apdu = 0;
    time_t elapsed_seconds = 0;
    time_t last_seconds = 0;
    time_t current_seconds = 0;
    time_t timeout_seconds = 0;
    int status = 0, i;

    // Set Property Tag
    BACNET_APPLICATION_TAG property_tag;
    if (device -> obj_types[obj_index] == OBJECT_BINARY_VALUE)
        property_tag = BACNET_APPLICATION_TAG_ENUMERATED;
    else
        property_tag = BACNET_APPLICATION_TAG_REAL;
    BACNET_APPLICATION_DATA_VALUE target_value;

    for (i = 0; i < MAX_PROPERTY_VALUES; i++) {
        target_value.context_specific = false;

        if (property_tag >= MAX_BACNET_APPLICATION_TAG) {
            fprintf(stderr, "Error: tag=%u - it must be less than %u\r\n",
                    property_tag, MAX_BACNET_APPLICATION_TAG);
            return 1;
        }
        status =
            bacapp_parse_application_data(property_tag, value,
                                          &target_value);
        if (!status) {
            printf("Error: unable to parse the tag value\r\n");
            return 1;
        }
        target_value.next = NULL;
        break;
    }

    /* configure the timeout values */
    last_seconds = time(NULL);
    timeout_seconds = (apdu_timeout() / 1000) * apdu_retries();

    /* try to bind with the device */
    found =
        address_bind_request(device -> instance, &max_apdu, &target_address);

    if (!found) {
        Send_WhoIs(device -> instance, device -> instance);
    }
    /* loop forever */
    request_invoke_id = 0;
    while (1) {
        /* increment timer - exit if timed out */
        current_seconds = time(NULL);

        /* at least one second has passed */
        if (current_seconds != last_seconds)
            tsm_timer_milliseconds((uint16_t) ((current_seconds -
                                                last_seconds) * 1000));
        if (error_detected)
            break;
        /* wait until the device is bound, or timeout and quit */
        if (!found) {
            found =
                address_bind_request(device -> instance, &max_apdu,
                                     &target_address);
        }
        if (found) {
            if (request_invoke_id == 0) {
                printf("request invoke set\n");
                write_acked = 0;
                request_invoke_id =
                    Send_Write_Property_Request(device -> instance,
                                                device -> obj_types[obj_index], device -> obj_ids[obj_index],
                                                PROP_PRESENT_VALUE, &target_value,
                                                MESSAGE_PRIORITY_NORMAL, -1);
            } else if (tsm_invoke_id_free(request_invoke_id))
                break;
            else if (tsm_invoke_id_failed(request_invoke_id)) {
                fprintf(stderr, "\rError: TSM Timeout!\r\n");
                tsm_free_invoke_id(request_invoke_id);
                error_detected = true;
                break;
            }
        } else {
            /* increment timer - exit if timed out */
            elapsed_seconds += (current_seconds - last_seconds);
            if (elapsed_seconds > timeout_seconds) {
                error_detected = true;
                printf("\rError: APDU Timeout!\r\n");
                break;
            }
        }

        /* returns 0 bytes on timeout */
        pdu_len = datalink_receive(&src, &rx_buf[0], MAX_MPDU, timeout);

        /* process */
        if (pdu_len) {
            npdu_handler(&src, &rx_buf[0], pdu_len);
        }

        /* keep track of time for next check */
        last_seconds = current_seconds;
    }
    if (error_detected)
    {
        error_detected = 0;
        return 1;
    }
    return 0;
}

void MyAbortHandler(
    BACNET_ADDRESS * src,
    uint8_t invoke_id,
    uint8_t abort_reason,
    bool server)
{
    (void) server;
    if (address_match(&target_address, src) &&
            (invoke_id == request_invoke_id)) {
        printf("BACnet Abort: %s\r\n",
               bactext_abort_reason_name((int) abort_reason));
        error_detected = TRUE;
    }
}
void MyRejectHandler(
    BACNET_ADDRESS * src,
    uint8_t invoke_id,
    uint8_t reject_reason)
{
    if (address_match(&target_address, src) &&
            (invoke_id == request_invoke_id)) {
        printf("BACnet Reject: %s\r\n",
               bactext_reject_reason_name((int) reject_reason));
        error_detected = TRUE;
    }
}

void My_Read_Property_Ack_Handler(
    uint8_t * service_request,
    uint16_t service_len,
    BACNET_ADDRESS * src,
    BACNET_CONFIRMED_SERVICE_ACK_DATA * service_data)
{
    int len = 0;
    BACNET_READ_PROPERTY_DATA data;

    if (address_match(&target_address, src) &&
            (service_data->invoke_id == request_invoke_id)) {
        len =
            rp_ack_decode_service_request(service_request, service_len,
                                          &data);
        if (len < 0) {
            printf("<decode failed!>\r\n");
        } else {
            rp_ack_sprintf_data(value, (size_t) value_length,&data);
        }

    }
}

void My_Write_Property_Ack_Handler(
    BACNET_ADDRESS * src,
    uint8_t invoke_id)
{
    if (address_match(&target_address, src) &&
            (invoke_id == request_invoke_id)) {
        printf("\r\nWriteProperty Acknowledged!\r\n");
        write_acked = 1;
    }
}

static void MyErrorHandler(
    BACNET_ADDRESS * src,
    uint8_t invoke_id,
    BACNET_ERROR_CLASS error_class,
    BACNET_ERROR_CODE error_code)
{
    if (address_match(&target_address, src) &&
            (invoke_id == request_invoke_id)) {
        printf("BACnet Error: %s: %s\r\n",
               bactext_error_class_name((int) error_class),
               bactext_error_code_name((int) error_code));
        error_detected = true;
    }
}

static void Init_Service_Handlers(
    void)
{
    Device_Init(NULL);
    /* we need to handle who-is
    *        to support dynamic device binding to us */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    /* handle i-am to support binding to other devices */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, handler_i_am_bind);
    /* set the handler for all the services we don't implement
    *        It is required to send the proper reject message... */
    apdu_set_unrecognized_service_handler_handler
    (handler_unrecognized_service);
    /* we must implement read property - it's required! */
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY,
                               handler_read_property);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY,
                               handler_read_property);
    /* handle the data coming back from confirmed requests */
    apdu_set_confirmed_simple_ack_handler(SERVICE_CONFIRMED_WRITE_PROPERTY,
                                          My_Write_Property_Ack_Handler);
    apdu_set_confirmed_ack_handler(SERVICE_CONFIRMED_READ_PROPERTY,
                                   My_Read_Property_Ack_Handler);

    /* handle any errors coming back */
    apdu_set_error_handler(SERVICE_CONFIRMED_READ_PROPERTY, MyErrorHandler);
    apdu_set_error_handler(SERVICE_CONFIRMED_WRITE_PROPERTY, MyErrorHandler);
    apdu_set_abort_handler(MyAbortHandler);
    apdu_set_reject_handler(MyRejectHandler);
}

/* END bacnet service functions */
