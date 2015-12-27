--[[
Copyright (C) 2013-2014 Draios inc.
 
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.


This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
--]]
view_info = 
{
	id = "spectro_file",
	name = "File Spectrogram",
	description = "XXX.",
	tips = {"XXX."},
	view_type = "spectrogram",
--	view_type = "table",
	applies_to = {"", "container.id", "proc.pid", "thread.tid", "proc.name", "evt.res", "k8s.pod.id", "k8s.rc.id", "k8s.svc.id", "k8s.ns.id"},
	filter = "evt.dir=< and fd.type=file",
	use_defaults = true,
	drilldown_target = "XXX",
	columns = 
	{
		{
			name = "NA",
			field = "evt.latency.log",
			is_key = true
		},
		{
			name = "LATENCY",
			description = "file latency.",
			field = "evt.latency.log",
		},
		{
			name = "COUNT",
			description = "XXX.",
			field = "evt.count",
			aggregation = "SUM",
			colsize = 8,
		}
	}
}
