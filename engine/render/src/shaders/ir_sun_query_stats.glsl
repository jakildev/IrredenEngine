#ifndef IR_SUN_QUERY_STATS_INCLUDED
#define IR_SUN_QUERY_STATS_INCLUDED

const uint kSunQueryCascades = 0u;
const uint kSunQueryIndexed = 1u;
const uint kSunQueryCandidates = 2u;
const uint kSunQueryFaceMisses = 3u;
const uint kSunQueryShapeCandidates = 4u;
const uint kSunQueryBlocked = 5u;
const uint kSunQueryFirstBlockerVisits = 6u;
const uint kSunQueryExhausted = 7u;
const uint kSunQueryIncomplete = 8u;
const uint kSunQueryOutside = 9u;
const uint kSunQueryCounterCount = 10u;

struct SunQueryStats {
    uint counts[kSunQueryCounterCount];
};

#endif
