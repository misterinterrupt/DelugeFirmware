#include "filter_config.h"
#include "mem_functions.h"
#include "util/container/enum_to_string_map.hpp"

// converts lpf/hpf mode to string for saving
// NOTE: "12dB"/"24dB" map to the LEGACY modes so that presets saved by older
// firmware (including official 4.1.4) load with the original filter behavior.
// The current community firmware modes use "12dBNew"/"24dBNew".
namespace deluge::dsp::filter {

EnumStringMap<FilterMode, kNumFilterModes> filterMap({{{FilterMode::TRANSISTOR_12DB_LEGACY, "12dB"},
                                                       {FilterMode::TRANSISTOR_24DB_LEGACY, "24dB"},
                                                       {FilterMode::TRANSISTOR_12DB, "12dBNew"},
                                                       {FilterMode::TRANSISTOR_24DB, "24dBNew"},
                                                       {FilterMode::TRANSISTOR_24DB_DRIVE, "24dBDrive"},
                                                       {FilterMode::SVF_BAND, "SVF_Band"},
                                                       {FilterMode::SVF_NOTCH, "SVF_Notch"},
                                                       {FilterMode::HPLADDER, "HPLadder"}}});

EnumStringMap<FilterRoute, kNumFilterRoutes> routeMap({{
    {FilterRoute::LOW_TO_HIGH, "L2H"},
    {FilterRoute::PARALLEL, "PARA"},
    {FilterRoute::HIGH_TO_LOW, "H2L"},
}});

} // namespace deluge::dsp::filter

char const* filterRouteToString(FilterRoute route) {
	return deluge::dsp::filter::routeMap(route);
}

FilterRoute stringToFilterRoute(char const* string) {
	return deluge::dsp::filter::routeMap(string);
}

FilterMode stringToLPFType(char const* string) {
	return deluge::dsp::filter::filterMap(string);
}
char const* lpfTypeToString(FilterMode mode) {
	return deluge::dsp::filter::filterMap(mode);
}
