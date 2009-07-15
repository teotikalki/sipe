/**
 * @file sipe-conf.c
 *
 * pidgin-sipe
 * 
 * Copyright (C) 2009 pier11 <pier11@kinozal.tv>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <string.h>
#include <glib.h>

#include "debug.h"

#ifdef _WIN32
#include "internal.h"
#endif

#include "sipe.h"
#include "sipe-conf.h"
#include "sipe-dialog.h"
#include "sipe-nls.h"
#include "sipe-utils.h"

/** 
 * AddUser request to Focus. 
 * Params:
 * focus_URI, from, request_id, focus_URI, from, endpoint_GUID
 */
#define SIPE_SEND_CONF_ADD_USER \
"<?xml version=\"1.0\"?>"\
"<request xmlns=\"urn:ietf:params:xml:ns:cccp\" xmlns:mscp=\"http://schemas.microsoft.com/rtc/2005/08/cccpextensions\" "\
	"C3PVersion=\"1\" "\
	"to=\"%s\" "\
	"from=\"%s\" "\
	"requestId=\"%d\">"\
	"<addUser>"\
		"<conferenceKeys confEntity=\"%s\"/>"\
		"<ci:user xmlns:ci=\"urn:ietf:params:xml:ns:conference-info\" entity=\"%s\">"\
			"<ci:roles>"\
				"<ci:entry>attendee</ci:entry>"\
			"</ci:roles>"\
			"<ci:endpoint entity=\"{%s}\" xmlns:msci=\"http://schemas.microsoft.com/rtc/2005/08/confinfoextensions\"/>"\
		"</ci:user>"\
	"</addUser>"\
"</request>"

static struct sip_im_session *
find_conf_session(struct sipe_account_data *sip,
		   const char *focus_uri)
{
	struct sip_im_session *session;
	GSList *entry;
	if (sip == NULL || focus_uri == NULL) {
		return NULL;
	}

	entry = sip->im_sessions;
	while (entry) {
		session = entry->data;
		if (session->focus_uri && !g_strcasecmp(focus_uri, session->focus_uri)) {
			return session;
		}
		entry = entry->next;
	}
	return NULL;
}

/** 
 * Generates random GUID.
 * This method is borrowed from pidgin's msnutils.c 
 */
static char *
rand_guid()
{
	return g_strdup_printf("%4X%4X-%4X-%4X-%4X-%4X%4X%4X",
			rand() % 0xAAFF + 0x1111,
			rand() % 0xAAFF + 0x1111,
			rand() % 0xAAFF + 0x1111,
			rand() % 0xAAFF + 0x1111,
			rand() % 0xAAFF + 0x1111,
			rand() % 0xAAFF + 0x1111,
			rand() % 0xAAFF + 0x1111,
			rand() % 0xAAFF + 0x1111);
}

static void
sipe_subscribe_conference(struct sipe_account_data *sip,
			  struct sip_im_session *session)
{
	gchar *contact = get_contact(sip);
	gchar *hdr = g_strdup_printf(
		"Event: conference\r\n"
		"Accept: application/conference-info+xml\r\n"
		"Supported: com.microsoft.autoextend\r\n"
		"Supported: ms-benotify\r\n"
		"Proxy-Require: ms-benotify\r\n"
		"Supported: ms-piggyback-first-notify\r\n"
		"Contact: %s\r\n",
		contact);
	g_free(contact);

	send_sip_request(sip->gc,
			 "SUBSCRIBE",
			 session->focus_uri,
			 session->focus_uri,
			 hdr,
			 "",
			 NULL,
			 process_subscribe_response);
	g_free(hdr);
}

/** Invite us to the focus callback */
static gboolean
process_invite_conf_focus_response(struct sipe_account_data *sip,
				   struct sipmsg *msg,
				   struct transaction *trans)
{
	struct sip_im_session * session = NULL;
	char *focus_uri = parse_from(sipmsg_find_header(msg, "To"));
	
	session = find_conf_session(sip, focus_uri);
	
	if (!session) {
		purple_debug_info("sipe", "process_invite_conf_focus_response: unable to find conf session with focus=%s\n", focus_uri);
		g_free(focus_uri);
		return FALSE;
	}
	
	if (!session->focus_dialog) {
		purple_debug_info("sipe", "process_invite_response: session's focus_dialog is NULL\n");
		g_free(focus_uri);
		return FALSE;
	}
	
	sipe_dialog_parse(session->focus_dialog, msg, TRUE);

	if (msg->response >= 200) {
		/* send ACK to focus */
		session->focus_dialog->cseq = 0;
		send_sip_request(sip->gc, "ACK", session->focus_dialog->with, session->focus_dialog->with, NULL, NULL, session->focus_dialog, NULL);
		session->focus_dialog->outgoing_invite = NULL;
		session->focus_dialog->is_established = TRUE;
	}
	
	if (msg->response >= 400) {
		purple_debug_info("sipe", "process_invite_conf_focus_response: INVITE response is not 200. Failed to join focus.\n");
		/* @TODO notify user of failure to join focus */				
		im_session_destroy(sip, session);
		g_free(focus_uri);
		return FALSE;
	} else if (msg->response == 200) {
		xmlnode *xn_response = xmlnode_from_str(msg->body, msg->bodylen);
		const gchar *code = xmlnode_get_attrib(xn_response, "code");
		if (!strcmp(code, "success")) {
			/* subscribe to focus */
			sipe_subscribe_conference(sip, session);
		}
		xmlnode_free(xn_response);
	}
	
	g_free(focus_uri);
	return TRUE;
}

/** Invite us to the focus */
static void 
sipe_invite_conf_focus(struct sipe_account_data *sip,
		       struct sip_im_session *session)
{
	gchar *hdr;
	gchar *contact;
	gchar *body;
	gchar *self;
	
	if (session->focus_dialog && session->focus_dialog->is_established) {
		purple_debug_info("sipe", "session with %s already has a dialog open\n", session->focus_uri);
		return;
	}

	if(!session->focus_dialog) {	
		session->focus_dialog = g_new0(struct sip_dialog, 1);		
		session->focus_dialog->callid = gencallid();
		session->focus_dialog->with = g_strdup(session->focus_uri);
		session->focus_dialog->endpoint_GUID = rand_guid();
	}
	if (!(session->focus_dialog->ourtag)) {
		session->focus_dialog->ourtag = gentag();
	}

	contact = get_contact(sip);
	hdr = g_strdup_printf(
		"Supported: ms-sender\r\n"
		"Contact: %s\r\n"
		"Content-Type: application/cccp+xml\r\n",
		contact);
	g_free(contact);
	
	/* @TODO put request_id to queue to further compare with incoming one */
	/* focus_URI, from, request_id, focus_URI, from, endpoint_GUID */
	self = sip_uri_self(sip);
	body = g_strdup_printf(
		SIPE_SEND_CONF_ADD_USER,
		session->focus_dialog->with,
		self,
		session->request_id++,
		session->focus_dialog->with,
		self,
		session->focus_dialog->endpoint_GUID);
	g_free(self);

	session->focus_dialog->outgoing_invite = send_sip_request(sip->gc,
								  "INVITE",
								  session->focus_dialog->with,
								  session->focus_dialog->with,
								  hdr,
								  body,
								  session->focus_dialog,
								  process_invite_conf_focus_response);
	g_free(body);
	g_free(hdr);
}

void process_incoming_invite_conf(struct sipe_account_data *sip,
				  struct sipmsg *msg)
{
	struct sip_im_session *session = NULL;
	struct sip_dialog *dialog = NULL;
	gchar *from = parse_from(sipmsg_find_header(msg, "From"));
	gchar *callid = sipmsg_find_header(msg, "Call-ID");
	xmlnode *xn_conferencing = xmlnode_from_str(msg->body, msg->bodylen);
	xmlnode *xn_focus_uri = xmlnode_get_child(xn_conferencing, "focus-uri");
	char *focus_uri = xmlnode_get_data(xn_focus_uri);

	xmlnode_free(xn_conferencing);	
	
	/* send OK */
	purple_debug_info("sipe", "We have received invitation to Conference. Focus URI=%s\n", focus_uri);
	send_sip_response(sip->gc, msg, 200, "OK", NULL);
	
	session = create_chat_session(sip);
	session->focus_uri = focus_uri;
	session->is_multiparty = FALSE;
	
	/* temporaty dialog with invitor */
	dialog = g_new0(struct sip_dialog, 1);
	dialog->callid = g_strdup(callid);
	dialog->with = from;
	sipe_dialog_parse(dialog, msg, FALSE);
	
	/* send BYE to invitor */
	send_sip_request(sip->gc, "BYE", dialog->with, dialog->with, NULL, NULL, dialog, NULL);
	sipe_dialog_free(dialog);
	
	//add self to conf
	sipe_invite_conf_focus(sip, session);       
}

void sipe_process_conference(struct sipe_account_data *sip,
			     struct sipmsg *msg)
{
	xmlnode *xn_conference_info;
	xmlnode *node;
	const gchar *focus_uri;
	struct sip_im_session *session;
	struct sip_dialog *dialog;

	if (msg->response != 0 && msg->response != 200) return;

	if (msg->bodylen == 0 || msg->body == NULL || strcmp(sipmsg_find_header(msg, "Event"), "conference")) return;

	xn_conference_info = xmlnode_from_str(msg->body, msg->bodylen);
	if (!xn_conference_info) return;

	focus_uri = xmlnode_get_attrib(xn_conference_info, "entity");
	session = find_conf_session(sip, focus_uri);

	if (!session) {
		purple_debug_info("sipe", "sipe_process_conference: unable to find conf session with focus=%s\n", focus_uri);
		return;
	}

	if (session->focus_uri && !session->conv) {
		gchar *chat_name = g_strdup_printf(_("Chat #%d"), ++sip->chat_seq);
		/* create prpl chat */
		session->conv = serv_got_joined_chat(sip->gc, session->chat_id, chat_name);
		session->chat_name = chat_name;
		/* @TODO ask for full state (re-subscribe) if it was a partial one -
		 * this is to obtain full list of conference participants.
		 */
	}

	/* IMMCU URI */
	if (!session->im_mcu_uri) {
		for (node = xmlnode_get_descendant(xn_conference_info, "conference-description", "conf-uris", "entry", NULL); 
		     node; 
		     node = xmlnode_get_next_twin(node))
		{
			gchar *purpose = xmlnode_get_data(xmlnode_get_child(node, "purpose"));

			if (purpose && !strcmp("chat", purpose)) {
				g_free(purpose);
				session->im_mcu_uri = xmlnode_get_data(xmlnode_get_child(node, "uri"));
				purple_debug_info("sipe", "sipe_process_conference: im_mcu_uri=%s\n", session->im_mcu_uri);
				break;
			}
			g_free(purpose);
		}
	}

	/* users */
	for (node = xmlnode_get_descendant(xn_conference_info, "users", "user", NULL); node; node = xmlnode_get_next_twin(node)) {
		xmlnode *endpoint = NULL;
		const gchar *user_uri = xmlnode_get_attrib(node, "entity");
		const gchar *state = xmlnode_get_attrib(node, "state");
		
		if (!strcmp("deleted", state)) {
			purple_conv_chat_remove_user(PURPLE_CONV_CHAT(session->conv),
						     user_uri, NULL /* reason */);
		} else {
			/* endpoints */
			for (endpoint = xmlnode_get_child(node, "endpoint"); endpoint; endpoint = xmlnode_get_next_twin(endpoint)) {	
				if (!strcmp("chat", xmlnode_get_attrib(endpoint, "session-type"))) {
					gchar *status = xmlnode_get_data(xmlnode_get_child(endpoint, "status"));
					if (!strcmp("connected", status)) {
						purple_conv_chat_add_user(PURPLE_CONV_CHAT(session->conv),
									  user_uri, NULL,
									  PURPLE_CBFLAGS_NONE, FALSE);
					}
					break;
				}
			}
		}
	}
	xmlnode_free(xn_conference_info);

	dialog = sipe_dialog_find(session, session->im_mcu_uri);
	if (!dialog) {
		dialog = sipe_dialog_add(session);

		dialog->callid = g_strdup(session->callid);
		dialog->with = g_strdup(session->im_mcu_uri);

		/* send INVITE to IMMCU */
		sipe_invite(sip, session, dialog->with, NULL, NULL, FALSE);
	}
}

/*
  Local Variables:
  mode: c
  c-file-style: "bsd"
  indent-tabs-mode: t
  tab-width: 8
  End:
*/
