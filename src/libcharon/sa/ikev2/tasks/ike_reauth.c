/*
 * Copyright (C) 2006-2008 Martin Willi
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

#include "ike_reauth.h"

#include <daemon.h>
#include <sa/ikev2/tasks/ike_delete.h>


typedef struct private_ike_reauth_t private_ike_reauth_t;

/**
 * Private members of a ike_reauth_t task.
 */
struct private_ike_reauth_t {

	/**
	 * Public methods and task_t interface.
	 */
	ike_reauth_t public;

	/**
	 * Assigned IKE_SA.
	 */
	ike_sa_t *ike_sa;

	/**
	 * reused ike_delete task
	 */
	ike_delete_t *ike_delete;
};

METHOD(task_t, build_i, status_t,
	private_ike_reauth_t *this, message_t *message)
{
	return this->ike_delete->task.build(&this->ike_delete->task, message);
}

METHOD(task_t, process_i, status_t,
	private_ike_reauth_t *this, message_t *message)
{
	ike_sa_t *new;
	host_t *host;
	enumerator_t *enumerator;
	child_sa_t *child_sa;
	peer_cfg_t *peer_cfg;

	/* process delete response first */
	this->ike_delete->task.process(&this->ike_delete->task, message);

	peer_cfg = this->ike_sa->get_peer_cfg(this->ike_sa);

	/* reauthenticate only if we have children */
	if (this->ike_sa->get_child_count(this->ike_sa) == 0
#ifdef ME
		/* we allow peers to reauth mediation connections (without children) */
		&& !peer_cfg->is_mediation(peer_cfg)
#endif /* ME */
		)
	{
		DBG1(DBG_IKE, "unable to reauthenticate IKE_SA, no CHILD_SA to recreate");
		return FAILED;
	}

	new = charon->ike_sa_manager->checkout_new(charon->ike_sa_manager,
								this->ike_sa->get_version(this->ike_sa), TRUE);
	if (!new)
	{	/* shouldn't happen */
		return FAILED;
	}

	new->set_peer_cfg(new, peer_cfg);
	host = this->ike_sa->get_other_host(this->ike_sa);
	new->set_other_host(new, host->clone(host));
	host = this->ike_sa->get_my_host(this->ike_sa);
	new->set_my_host(new, host->clone(host));
	/* if we already have a virtual IP, we reuse it */
	host = this->ike_sa->get_virtual_ip(this->ike_sa, TRUE);
	if (host)
	{
		new->set_virtual_ip(new, TRUE, host);
	}

#ifdef ME
	/* we initiate the new IKE_SA of the mediation connection without CHILD_SA */
	if (peer_cfg->is_mediation(peer_cfg))
	{
		if (new->initiate(new, NULL, 0, NULL, NULL) == DESTROY_ME)
		{
			charon->ike_sa_manager->checkin_and_destroy(
								charon->ike_sa_manager, new);
			/* set threads active IKE_SA after checkin */
			charon->bus->set_sa(charon->bus, this->ike_sa);
			DBG1(DBG_IKE, "reauthenticating IKE_SA failed");
			return FAILED;
		}
	}
#endif /* ME */

	enumerator = this->ike_sa->create_child_sa_enumerator(this->ike_sa);
	while (enumerator->enumerate(enumerator, (void**)&child_sa))
	{
		switch (child_sa->get_state(child_sa))
		{
			case CHILD_ROUTED:
			{
				/* move routed child directly */
				this->ike_sa->remove_child_sa(this->ike_sa, enumerator);
				new->add_child_sa(new, child_sa);
				break;
			}
			default:
			{
				/* initiate/queue all child SAs */
				child_cfg_t *child_cfg = child_sa->get_config(child_sa);
				child_cfg->get_ref(child_cfg);
				if (new->initiate(new, child_cfg, 0, NULL, NULL) == DESTROY_ME)
				{
					enumerator->destroy(enumerator);
					charon->ike_sa_manager->checkin_and_destroy(
										charon->ike_sa_manager, new);
					/* set threads active IKE_SA after checkin */
					charon->bus->set_sa(charon->bus, this->ike_sa);
					DBG1(DBG_IKE, "reauthenticating IKE_SA failed");
					return FAILED;
				}
				break;
			}
		}
	}
	enumerator->destroy(enumerator);
	charon->ike_sa_manager->checkin(charon->ike_sa_manager, new);
	/* set threads active IKE_SA after checkin */
	charon->bus->set_sa(charon->bus, this->ike_sa);

	/* we always destroy the obsolete IKE_SA */
	return DESTROY_ME;
}

METHOD(task_t, get_type, task_type_t,
	private_ike_reauth_t *this)
{
	return TASK_IKE_REAUTH;
}

METHOD(task_t, migrate, void,
	private_ike_reauth_t *this, ike_sa_t *ike_sa)
{
	this->ike_delete->task.migrate(&this->ike_delete->task, ike_sa);
	this->ike_sa = ike_sa;
}

METHOD(task_t, destroy, void,
	private_ike_reauth_t *this)
{
	this->ike_delete->task.destroy(&this->ike_delete->task);
	free(this);
}

/*
 * Described in header.
 */
ike_reauth_t *ike_reauth_create(ike_sa_t *ike_sa)
{
	private_ike_reauth_t *this;

	INIT(this,
		.public = {
			.task = {
				.get_type = _get_type,
				.migrate = _migrate,
				.build = _build_i,
				.process = _process_i,
				.destroy = _destroy,
			},
		},
		.ike_sa = ike_sa,
		.ike_delete = ike_delete_create(ike_sa, TRUE),
	);

	return &this->public;
}