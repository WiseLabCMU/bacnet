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

#ifndef ADAPTER_H
#define ADAPTER_H

#include "mio.h"

/** Defines for supported adapter types */
#define ENFUSENAME "enfuse"
#define HUENAME "hue"
#define BACNETNAME "bacnet"
#define MODBUSNAME "modbus"
#define PUPNAME "pup"
#define B3GPIONAME "b3"

/** A struct used to hold general adapter information.
 */
typedef struct adapter {
    char* jid; /** Jabber unique ID */
    char* password; /** Password for jid */
    char* xmpp_server;  /** Domain of MIO XMPP server */
    int xmpp_port; /** Port the MIO XMPP listens on */
    int polling_rate; /**  */
    mio_conn_t* connection;

    int adapterfd;
    int register_port;
    struct sockaddr_in *ipc_addr;

    char* type;
    void* context;
    pthread_mutex_t* context_lock;
    pthread_t *data_thread;
    pthread_t *actuate_thread;
    char* config_dir;
    void* (*actuate) (void *);
    void* (*data) (void *);
    void* (*parser) (void *);
} adapter_t;

#endif
