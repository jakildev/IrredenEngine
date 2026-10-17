#ifndef IR_SUN_QUERY_STATS_INCLUDED
#define IR_SUN_QUERY_STATS_INCLUDED

constant uint kSunQueryCascades = 0u;
constant uint kSunQueryIndexed = 1u;
constant uint kSunQueryCandidates = 2u;
constant uint kSunQueryFaceMisses = 3u;
constant uint kSunQueryShapeCandidates = 4u;
constant uint kSunQueryBlocked = 5u;
constant uint kSunQueryFirstBlockerVisits = 6u;
constant uint kSunQueryExhausted = 7u;
constant uint kSunQueryIncomplete = 8u;
constant uint kSunQueryOutside = 9u;
constant uint kSunQueryCounterCount = 10u;

struct SunQueryStats {
    uint counts[kSunQueryCounterCount];
};

#endif
