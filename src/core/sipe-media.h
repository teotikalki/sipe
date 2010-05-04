/**
 * @file sipe-media.h
 *
 * pidgin-sipe
 *
 * Copyright (C) 2010 Jakub Adam <jakub.adam@tieto.com>
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

/* Forward declarations */
struct sipmsg;
struct sipe_core_private;
struct sipe_media_call_private;

/**
 * @TODO: documentation!!!
 */
void sipe_media_incoming_invite(struct sipe_core_private *sipe_private,
				struct sipmsg *msg);

/**
 * @TODO: documentation!!!
 */
void sipe_media_hangup(struct sipe_core_private *sipe_private);

/**
 * @TODO: documentation!!!???
 */
gchar *sipe_media_get_callid(struct sipe_media_call_private *call_private);
