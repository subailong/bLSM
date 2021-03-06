/*
 * tcpclient.cpp
 *
 * Copyright 2010-2012 Yahoo! Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *  Created on: Feb 2, 2010
 *      Author: sears
 */

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <assert.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "tcpclient.h"
#include "datatuple.h"
#include "network.h"
extern "C" {
	#define DEBUG(...) /* */
}

struct logstore_handle_t {
	char *host;
	int portnum;
	int timeout;
	struct sockaddr_in serveraddr;
	struct hostent* server;
	int server_socket;
  FILE * server_fsocket;
};

logstore_handle_t * logstore_client_open(const char *host, int portnum, int timeout) {
	logstore_handle_t *ret = (logstore_handle_t*) malloc(sizeof(*ret));
	ret->host = strdup(host);
	ret->portnum = portnum;
	if(ret->portnum == 0) { ret->portnum = 32432; }
	ret->timeout = timeout;
        ret->server_socket = -1;
	ret->server_fsocket = NULL;

    ret->server = gethostbyname(ret->host);
    if (ret->server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", ret->host);
        free(ret->host); free(ret); return 0;
    }

    /* build the server's Internet address */
    bzero((char *) &ret->serveraddr, sizeof(ret->serveraddr));
    ret->serveraddr.sin_family = AF_INET;
    bcopy((char *)ret->server->h_addr,
	  (char *)&ret->serveraddr.sin_addr.s_addr, ret->server->h_length);
    ret->serveraddr.sin_port = htons(ret->portnum);

    DEBUG("LogStore start\n");

    return ret;
}

static inline void close_conn(logstore_handle_t *l) {
  perror("read/write err.. conn closed.\n");
  fclose(l->server_fsocket); //close the connection
  l->server_fsocket = NULL;
  l->server_socket = -1;
}

uint8_t
logstore_client_op_returns_many(logstore_handle_t *l,
				uint8_t opcode,  dataTuple * tuple, dataTuple * tuple2, uint64_t count) {

    if(l->server_socket < 0)
    {
      l->server_socket = socket(AF_INET, SOCK_STREAM, 0);
      l->server_fsocket = fdopen(l->server_socket, "a+");
        if (l->server_socket < 0)
        {
            perror("ERROR opening socket.\n");
			return LOGSTORE_CONN_CLOSED_ERROR;
        }

#ifdef LOGSTORE_NODELAY
        int flag = 1;
        int result = setsockopt(l->server_socket,            /* socket affected */
                                IPPROTO_TCP,     /* set option at TCP level */
                                TCP_NODELAY,     /* name of option */
                                (char *) &flag,  /* the cast is historical
                                                    cruft */
                                sizeof(int));    /* length of option value */
        if (result < 0)
        {
            perror("ERROR on setting socket option TCP_NODELAY.\n");
            return LOGSTORE_CONN_CLOSED_ERROR;
        }
#endif
        /* connect: create a connection with the server */
        if (connect(l->server_socket, (sockaddr*) &(l->serveraddr), sizeof(l->serveraddr)) < 0)
        {
            perror("ERROR connecting\n");
            return LOGSTORE_CONN_CLOSED_ERROR;
        }

        DEBUG("sock opened %d\n", l->server_socket);
    }

    network_op_t err = 0;

    //send the opcode
    if( !err) { err = writetosocket(l->server_fsocket, &opcode, sizeof(opcode));  }

    //send the first tuple
    if( !err) { err = writetupletosocket(l->server_fsocket, tuple);               }

    //send the second tuple
    if( !err) { err = writetupletosocket(l->server_fsocket, tuple2);              }

    if( (!err) && (count != (uint64_t)-1) ) {
                err = writecounttosocket(l->server_fsocket, count);               }

    network_op_t rcode = LOGSTORE_CONN_CLOSED_ERROR;
    if( !err) {
      rcode = readopfromsocket(l->server_fsocket,LOGSTORE_SERVER_RESPONSE);
    }

    if( opiserror(rcode) ) { close_conn(l); }

    return rcode;

}
network_op_t
logstore_client_send_tuple(logstore_handle_t *l, dataTuple *t) {
  assert(l->server_fsocket != 0);
  network_op_t rcode = LOGSTORE_RESPONSE_SUCCESS;
  int err;
  if(t) {
    err =  writetupletosocket(l->server_fsocket, t);
  } else {
    err = writeendofiteratortosocket(l->server_fsocket);
    if(!err) {
      rcode = readopfromsocket(l->server_fsocket, LOGSTORE_SERVER_RESPONSE);
    }
  }
  if(err) {
    close_conn(l);
    rcode = LOGSTORE_CONN_CLOSED_ERROR;
  }
  return rcode;
}

dataTuple *
logstore_client_next_tuple(logstore_handle_t *l) {
	assert(l->server_fsocket != 0); // otherwise, then the client forgot to check a return value...
	int err = 0;
	dataTuple * ret = readtuplefromsocket(l->server_fsocket, &err);
	if(err) {
		close_conn(l);
		if(ret) {
			dataTuple::freetuple(ret);
			ret = NULL;
		}
	}
	return ret;
}
dataTuple *
logstore_client_op(logstore_handle_t *l,
          uint8_t opcode,  dataTuple * tuple, dataTuple * tuple2, uint64_t count)
{
    network_op_t rcode = logstore_client_op_returns_many(l, opcode, tuple, tuple2, count);

    if(opiserror(rcode)) { return NULL; }

    dataTuple * ret = NULL;

    if(rcode == LOGSTORE_RESPONSE_SENDING_TUPLES)
    {
		ret =     logstore_client_next_tuple(l);
		if(ret) {
			dataTuple *nxt = logstore_client_next_tuple(l);
			if(nxt) {
				fprintf(stderr, "Opcode %d returned multiple tuples, but caller expects zero or one.  Closing connection.\n", (int)opcode);
				dataTuple::freetuple(nxt);
				dataTuple::freetuple(ret);
				close_conn(l);
				ret = 0;
			}
		}
    } else if(rcode == LOGSTORE_RESPONSE_SUCCESS) {
    	ret = tuple ? tuple : dataTuple::create("", 1);
    } else {
    	assert(rcode == LOGSTORE_RESPONSE_FAIL); // if this is an invalid response, we should have noticed above
    	ret = 0;
    }
    return ret;
}

int logstore_client_close(logstore_handle_t* l) {
    if(l->server_fsocket)
    {
        writetosocket(l->server_fsocket, (char*) &OP_DONE, sizeof(uint8_t));

        fclose(l->server_fsocket);
        DEBUG("socket closed %d\n.", l->server_fsocket);
    }
    free(l->host);
    free(l);
    return 0;
}
