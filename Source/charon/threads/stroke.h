/**
 * @file stroke.h
 *
 * @brief Interface of stroke_t.
 *
 */

/*
 * Copyright (C) 2006 Martin Willi
 * Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#ifndef STROKE_H_
#define STROKE_H_

#include <config/policy_store.h>
#include <config/connection_store.h>
#include <config/credential_store.h>


#define STROKE_SOCKET "/var/run/charon.ctl"

/**
 * @brief A stroke message sent over the unix socket.
 * 
 */
typedef struct stroke_msg_t stroke_msg_t;

struct stroke_msg_t {
	/* length of this message with all strings */
	u_int16_t length;
	/* type of the message */
	enum {
		/* initiate a connection */
		STR_INITIATE,
		/* install SPD entries for a connection */
		STR_INSTALL,
		/* add a connection */
		STR_ADD_CONN,
		/* delete a connection */
		STR_DEL_CONN,
		/* more to come */
	} type;
	union {
		/* data for STR_INITIATE, STR_INSTALL */
		struct {
			char *name;
		} initiate, install;
		/* data for STR_ADD_CONN */
		struct {
			char *name;
			struct {
				char *id;
				char *address;
				char *subnet;
				u_int8_t subnet_mask;
			} me, other;
		} add_conn;
	};
	u_int8_t buffer[];
};


typedef struct stroke_t stroke_t;

/**
 * @brief Stroke is a configuration and control interface which
 * allows other processes to modify charons behavior.
 * 
 * stroke_t allows config manipulation (as whack in pluto). 
 * Messages of type stroke_msg_t's are sent over a unix socket
 * (/var/run/charon.ctl). stroke_t implements the connections_t
 * and the policies_t interface, which means it acts as a 
 * configuration backend for those too. stroke_t uses an own
 * thread to read from the socket.
 * 
 * @warning DO NOT cast stroke_t to any of the implemented interfaces!
 * stroke_t implements multiple interfaces, so you must use
 * stroke_t.interface_xy to access the specific interface! You have
 * been warned...
 * 
 * @todo Add clean thread cancellation
 * 
 * @b Constructors:
 * - stroke_create()
 * 
 * @ingroup threads
 */
struct stroke_t {

	/**
	 * Implements connection_store_t interface
	 */
	connection_store_t connections;
	
	/**
	 * Implements policy_store_t interface
	 */
	policy_store_t policies;
	
	/**
	 * Implements credential_store_t interfacce
	 */
	credential_store_t credentials;
	
	/**
	 * @brief Destroy a stroke_t instance.
	 * 
	 * @param this		stroke_t objec to destroy
	 */
	void (*destroy) (stroke_t *this);
};


/**
 * @brief Create the stroke interface and listen on the socket.
 * 
 * @return stroke_t object
 * 
 * @ingroup threads
 */
stroke_t *stroke_create();

#endif /* STROKE_H_ */
