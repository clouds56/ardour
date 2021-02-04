/*
 * Copyright (C) 1999-2017 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2011-2014 David Robillard <d@drobilla.net>
 * Copyright (C) 2015 Robin Gareus <robin@gareus.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <boost/bind.hpp>

#include "pbd/error.h"
#include "pbd/compose.h"

#include "ardour/audioengine.h"
#include "ardour/monitor_control.h"
#include "ardour/route.h"
#include "ardour/session.h"
#include "ardour/track.h"
#include "ardour/vca_manager.h"

#include "pbd/i18n.h"

using namespace std;
using namespace PBD;
using namespace ARDOUR;
using namespace Glib;

void
Session::set_controls (boost::shared_ptr<ControlList> cl, double val, Controllable::GroupControlDisposition gcd)
{
	if (cl->empty()) {
		return;
	}

	for (ControlList::iterator ci = cl->begin(); ci != cl->end(); ++ci) {
		/* as of july 2017 this is a no-op for everything except record enable */
		(*ci)->pre_realtime_queue_stuff (val, gcd);
	}

	queue_event (get_rt_event (cl, val, gcd));
}

void
Session::set_control (boost::shared_ptr<AutomationControl> ac, double val, Controllable::GroupControlDisposition gcd)
{
	if (!ac) {
		return;
	}

	boost::shared_ptr<ControlList> cl (new ControlList);
	cl->push_back (ac);
	set_controls (cl, val, gcd);
}

void
Session::rt_set_controls (boost::shared_ptr<ControlList> cl, double val, Controllable::GroupControlDisposition gcd)
{
	/* Note that we require that all controls in the ControlList are of the
	   same type.
	*/
	if (cl->empty()) {
		return;
	}

	for (ControlList::iterator c = cl->begin(); c != cl->end(); ++c) {
		(*c)->set_value (val, gcd);
	}

	/* some controls need global work to take place after they are set. Do
	 * that here.
	 */

	switch (cl->front()->parameter().type()) {
	case SoloAutomation:
		update_route_solo_state ();
		break;
	default:
		break;
	}
}

void
Session::prepare_exclusive_solo (boost::shared_ptr<RouteList> soloed, boost::shared_ptr<RouteList> unsoloed)
{
	if (unsoloed) {
		unsoloed->clear ();
	}

	boost::shared_ptr<RouteList> rl (new RouteList);
	boost::shared_ptr<RouteList> routes = get_routes();

	for (RouteList::const_iterator i = routes->begin(); i != routes->end(); ++i) {
#ifdef MIXBUS
		if ((0 == _route->mixbus()) != (0 == (*i)->mixbus ())) {
			continue;
		}
#endif
		if ((*i)->soloed ()) {
			rl->push_back (*i);
		} else if (unsoloed) {
			unsoloed->push_back (*i);
		}
	}

	set_controls (route_list_to_control_list (rl, &Stripable::solo_control), false, Controllable::UseGroup);

	if (soloed) {
		soloed.swap (rl);
	}
}

void
Session::clear_all_solo_state (boost::shared_ptr<RouteList> rl)
{
	queue_event (get_rt_event (rl, false, rt_cleanup, Controllable::NoGroup, &Session::rt_clear_all_solo_state));
}

void
Session::rt_clear_all_solo_state (boost::shared_ptr<RouteList> rl, bool /* yn */, Controllable::GroupControlDisposition /* group_override */)
{
	for (RouteList::iterator i = rl->begin(); i != rl->end(); ++i) {
		if ((*i)->is_auditioner()) {
			continue;
		}
		(*i)->clear_all_solo_state();
	}

	_vca_manager->clear_all_solo_state ();

	update_route_solo_state ();
}

void
Session::process_rtop (SessionEvent* ev)
{
	ev->rt_slot ();

	if (ev->event_loop) {
		ev->event_loop->call_slot (MISSING_INVALIDATOR, boost::bind (ev->rt_return, ev));
	} else {
		warning << string_compose ("programming error: %1", X_("Session RT event queued from thread without a UI - cleanup in RT thread!")) << endmsg;
		ev->rt_return (ev);
	}
}
