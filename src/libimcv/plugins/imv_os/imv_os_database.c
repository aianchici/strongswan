/*
 * Copyright (C) 2012 Andreas Steffen
 * HSR Hochschule fuer Technik Rapperswil
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

#include "imv_os_database.h"

#include <utils/debug.h>

#include <string.h>

typedef struct private_imv_os_database_t private_imv_os_database_t;

/**
 * Private data of a imv_os_database_t object.
 *
 */
struct private_imv_os_database_t {

	/**
	 * Public imv_os_database_t interface.
	 */
	imv_os_database_t public;

	/**
	 * database instance
	 */
	database_t *db;

};

METHOD(imv_os_database_t, check_packages, status_t,
	private_imv_os_database_t *this, imv_os_state_t *state,
	enumerator_t *package_enumerator)
{
	char *product, *package, *release, *cur_release;
	u_char *pos;
	chunk_t os_name, os_version, name, version;
	os_type_t os_type;
	size_t os_version_len;
	os_package_state_t package_state;
	int pid, gid;
	int count = 0, count_ok = 0, count_no_match = 0, count_blacklist = 0;
	enumerator_t *e;
	status_t status = SUCCESS;
	bool found, match;

	state->get_info(state, &os_type, &os_name, &os_version);

	if (os_type == OS_TYPE_ANDROID)
	{
		/*no package dependency on Android version */
		product = strdup(enum_to_name(os_type_names, os_type));
	}
	else
	{
		/* remove appended platform info */
		pos = memchr(os_version.ptr, ' ', os_version.len);
		os_version_len = pos ? (pos - os_version.ptr) : os_version.len;
		product = malloc(os_name.len + 1 + os_version_len + 1);
		sprintf(product, "%.*s %.*s", (int)os_name.len, os_name.ptr,
									  (int)os_version_len, os_version.ptr);
	}
	DBG1(DBG_IMV, "processing installed '%s' packages", product);

	/* Get primary key of product */
	e = this->db->query(this->db,
				"SELECT id FROM products WHERE name = ?",
				DB_TEXT, product, DB_INT);
	if (!e)
	{
		free(product);
		return FAILED;
	}
	if (!e->enumerate(e, &pid))
	{
		e->destroy(e);
		free(product);
		return NOT_FOUND;
	}
	e->destroy(e);

	while (package_enumerator->enumerate(package_enumerator, &name, &version))
	{
		/* Convert package name chunk to a string */
		package = strndup(name.ptr, name.len);
		count++;

		/* Get primary key of package */
		e = this->db->query(this->db,
					"SELECT id FROM packages WHERE name = ?",
					DB_TEXT, package, DB_INT);
		if (!e)
		{
			free(product);
			free(package);
			return FAILED;
		}
		if (!e->enumerate(e, &gid))
		{
			/* package not present in database for any product - skip */
			if (os_type == OS_TYPE_ANDROID)
			{
				DBG2(DBG_IMV, "package '%s' (%.*s) not found",
					 package, version.len, version.ptr);
			}
			free(package);
			e->destroy(e);
			continue;
		}
		e->destroy(e);

		/* Convert package version chunk to a string */
		release = strndup(version.ptr, version.len);

		/* Enumerate over all acceptable versions */
		e = this->db->query(this->db,
				"SELECT release, security FROM versions "
				"WHERE product = ? AND package = ?",
				DB_INT, pid, DB_INT, gid, DB_TEXT, DB_INT);
		if (!e)
		{
			free(product);
			free(package);
			free(release);
			return FAILED;
		}
		found = FALSE;
		match = FALSE;

		while (e->enumerate(e, &cur_release, &package_state))
		{
			found = TRUE;
			if (streq(release, cur_release) || streq("*", cur_release))
			{
				match = TRUE;
				break;
			}
		}
		e->destroy(e);

		if (found)
		{
			if (match)
			{
				if (package_state == OS_PACKAGE_STATE_BLACKLIST)
				{
					DBG2(DBG_IMV, "package '%s' (%s) is blacklisted",
								   package, release);
					count_blacklist++;
					state->add_bad_package(state, package, package_state);
				}
				else
				{
					DBG2(DBG_IMV, "package '%s' (%s)%N is ok", package, release,
								   os_package_state_names, package_state);
					count_ok++;
				}
			}
			else
			{
				DBG1(DBG_IMV, "package '%s' (%s) no match", package, release);
				count_no_match++;
				state->add_bad_package(state, package, package_state);
			}
		}
		else
		{
			/* package not present in database for this product - skip */
		}
		free(package);
		free(release);
	}
	free(product);
	state->set_count(state, count, count_no_match, count_blacklist, count_ok);

	return status;
}

METHOD(imv_os_database_t, set_device_info, void,
	private_imv_os_database_t *this,  int session_id, int count,
	int count_update, int count_blacklist, u_int flags)
{
	enumerator_t *e;

	this->db->execute(this->db, NULL,
			"INSERT INTO device_infos (session, count, count_update, "
			"count_blacklist, flags) VALUES (?, ?, ?, ?, ?)",
			 DB_INT, session_id, DB_INT, count, DB_INT, count_update,
			 DB_INT, count_blacklist, DB_UINT, flags);
}

METHOD(imv_os_database_t, destroy, void,
	private_imv_os_database_t *this)
{
	free(this);
}

/**
 * See header
 */
imv_os_database_t *imv_os_database_create(imv_database_t *imv_db)
{
	private_imv_os_database_t *this;

	if (!imv_db)
	{
		return NULL;
	}

	INIT(this,
		.public = {
			.check_packages = _check_packages,
			.set_device_info = _set_device_info,
			.destroy = _destroy,
		},
		.db = imv_db->get_database(imv_db),
	);

	return &this->public;
}

