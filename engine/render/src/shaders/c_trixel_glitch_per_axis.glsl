#version 450 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_glitch_common.glsl"
#include "ir_per_axis_cell_dispatch.glsl"

layout(std430, binding = 8) readonly buffer TrixelGlitchSubjects {
    uvec4 subjectWords[];
};
layout(std430, binding = 28) readonly buffer TrixelGlitchTileList {
    uint tileList[];
};

layout(rgba8, binding = 0) readonly uniform image2D liveColors;
layout(r32i, binding = 1) readonly uniform iimage2D liveDistances;
layout(rg32ui, binding = 2) readonly uniform uimage2D liveEntityIds;
layout(rgba8, binding = 3) readonly uniform image2D historyColors;
layout(rg32ui, binding = 4) readonly uniform uimage2D historyEntityIds;
layout(rgba8, binding = 5) writeonly uniform image2D scratchColors;

const int kTileSize = 16;
const int kTileHalo = 10;
const int kTileWindow = kTileSize + 2 * kTileHalo;
const int kLevel1Size = 28;
const int kLevel2Size = 24;
const int kLevel3Size = 20;
const int kLevel4Size = 16;
const int kPresenceWords = 32;
const int kMaxRankedSubjects = 32;
const int kMaxIndexedSubjects = kPresenceWords * 32;
shared int stagedSlots[kTileWindow * kTileWindow];
shared int stagedSpills[kTileWindow * kTileWindow];
shared uint stagedBits[kTileWindow * kTileWindow];
shared uint level1[kLevel1Size * kLevel1Size];
shared uint level2[kLevel2Size * kLevel2Size];
shared uint level3[kLevel3Size * kLevel3Size];
shared uint level4[kLevel4Size * kLevel4Size];
shared uint presentWords[kPresenceWords];
shared uint rankBases[kPresenceWords];
shared int rankToSlot[kMaxRankedSubjects];
shared uint spillMasks[kGlitchSubjectMaxSpill + 1];
shared uint rankedSubjectCount;

struct SubjectRecord {
    uvec2 id;
    uint salt;
    int spill;
    bool depthEnabled;
    uint tearThreshold;
    uint holdThreshold;
    uint dissolveThreshold;
    uint echoStrength;
    int tearMaxTrixels;
    int splitTrixels;
    int bandRows;
    int dissolveCell;
    ivec2 echoA;
    ivec2 echoB;
    int depthNear;
    int depthFar;
    int tintA;
    int tintB;
};

struct PerAxisFrame {
    ivec2 size;
    ivec2 storeOrigin;
    ivec2 historyOrigin;
    int faceStep;
    int depthScale;
    int depthBias;
    uint seed;
    uvec2 tick;
};

bool inCanvas(ivec2 q, ivec2 size) {
    return q.x >= 0 && q.y >= 0 && q.x < size.x && q.y < size.y;
}

int floorDivPositive(int value, int divisor) {
    int quotient = value / divisor;
    int remainder = value % divisor;
    return quotient - (remainder < 0 ? 1 : 0);
}

uvec4 loadColour(ivec2 q) {
    return uvec4(imageLoad(liveColors, q) * 255.0 + 0.5);
}

uvec2 loadId(ivec2 q) {
    return decodeEntityId(imageLoad(liveEntityIds, q).xy);
}

int subjectSlot(uvec2 id) {
    if (all(equal(id, uvec2(0u)))) {
        return -1;
    }
    uint mask = subjectWords[1].x;
    uint tableBase = subjectWords[1].y;
    uint entry = hash3(id.x, id.y, kGlitchSubjectTableTag) & mask;
    for (uint probe = 0u; probe <= mask; ++probe) {
        uvec4 word = subjectWords[tableBase + entry];
        if (all(equal(word.xy, uvec2(0u)))) {
            return -1;
        }
        if (all(equal(word.xy, id))) {
            return int(word.z);
        }
        entry = (entry + 1u) & mask;
    }
    return -1;
}

uint recordBase(int slot) {
    return subjectWords[1].z + uint(slot) * kGlitchSubjectRecordWords;
}

int recordSpill(int slot) {
    return int(subjectWords[recordBase(slot)].w & 0xFFu);
}

SubjectRecord loadRecord(int slot) {
    uint base = recordBase(slot);
    uvec4 w0 = subjectWords[base];
    uvec4 w1 = subjectWords[base + 1u];
    ivec4 w2 = ivec4(subjectWords[base + 2u]);
    ivec4 w3 = ivec4(subjectWords[base + 3u]);
    ivec4 w4 = ivec4(subjectWords[base + 4u]);
    SubjectRecord record;
    record.id = w0.xy;
    record.salt = w0.z;
    record.spill = int(w0.w & 0xFFu);
    record.depthEnabled = (w0.w & 0x100u) != 0u;
    record.tearThreshold = w1.x;
    record.holdThreshold = w1.y;
    record.dissolveThreshold = w1.z;
    record.echoStrength = w1.w;
    record.tearMaxTrixels = w2.x;
    record.splitTrixels = w2.y;
    record.bandRows = w2.z;
    record.dissolveCell = w2.w;
    record.echoA = w3.xy;
    record.echoB = w3.zw;
    record.depthNear = w4.x;
    record.depthFar = w4.y;
    record.tintA = w4.z;
    record.tintB = w4.w;
    return record;
}

int stagedSlotAt(ivec2 q, ivec2 tileOrigin);

bool isEdgeStaged(ivec2 q, int slot, ivec2 tileOrigin) {
    if (stagedSlotAt(q, tileOrigin) != slot) {
        return false;
    }
    return stagedSlotAt(q + ivec2(2, 0), tileOrigin) != slot ||
           stagedSlotAt(q - ivec2(2, 0), tileOrigin) != slot ||
           stagedSlotAt(q + ivec2(0, 2), tileOrigin) != slot ||
           stagedSlotAt(q - ivec2(0, 2), tileOrigin) != slot;
}

uvec4 echoBlend(uvec4 colour, uvec4 tint, uint strength) {
    if (colour.a == 0u) {
        return uvec4(tint.rgb, (255u * strength) >> 8);
    }
    uvec4 mixed = (tint * strength + colour * (256u - strength)) >> 8;
    return uvec4(mixed.rgb, colour.a);
}

uvec4 applySubject(
    ivec2 p, int slot, uvec2 acceptId, ivec2 tileOrigin, PerAxisFrame frame
) {
    SubjectRecord record = loadRecord(slot);
    uvec2 current = loadId(p);
    int encodedDistance = imageLoad(liveDistances, p).x;
    int depth = decodeDepthPerAxis(encodedDistance) * frame.depthScale + frame.depthBias;
    bool depthPasses = !record.depthEnabled ||
        (encodedDistance < 0x7FFFFFFF && depth >= record.depthNear && depth <= record.depthFar);
    ivec2 relative = p - frame.storeOrigin;
    int band = floorDivPositive(relative.y, record.bandRows * frame.faceStep);
    uint bandLane = record.salt ^ (uint(band) * kGlitchCellHashY);

    ivec2 source = p;
    if (record.tearThreshold > 0u && depthPasses) {
        uint hash = glitchHash(frame.seed, frame.tick, kGlitchEffectSubjectTear, bandLane);
        if ((hash & 0xFFFFu) < record.tearThreshold) {
            ivec2 shifted = p + ivec2(
                glitchSignedOffset(hash >> 16, record.tearMaxTrixels) * frame.faceStep,
                0
            );
            if (inCanvas(shifted, frame.size) && all(equal(loadId(shifted), acceptId))) {
                source = shifted;
            }
        }
    }
    uvec4 colour = loadColour(source);

    if (record.splitTrixels != 0) {
        ivec2 split = ivec2(record.splitTrixels * frame.faceStep, 0);
        ivec2 redAt = source + split;
        ivec2 blueAt = source - split;
        uvec4 red = inCanvas(redAt, frame.size) && all(equal(loadId(redAt), acceptId))
            ? loadColour(redAt) : colour;
        uvec4 blue = inCanvas(blueAt, frame.size) && all(equal(loadId(blueAt), acceptId))
            ? loadColour(blueAt) : colour;
        colour = uvec4(red.r, colour.g, blue.b, max(max(red.a, colour.a), blue.a));
    }

    if (record.holdThreshold > 0u && depthPasses) {
        uint hash = glitchHash(frame.seed, frame.tick, kGlitchEffectSubjectHold, bandLane);
        if ((hash & 0xFFFFu) < record.holdThreshold) {
            ivec2 historyAt = p - frame.storeOrigin + frame.historyOrigin;
            bool historyInBounds = inCanvas(historyAt, frame.size);
            uvec2 held = historyInBounds
                ? decodeEntityId(imageLoad(historyEntityIds, historyAt).xy) : uvec2(0u);
            if (all(equal(held, record.id)) ||
                (all(equal(current, record.id)) && all(equal(held, uvec2(0u))))) {
                colour = historyInBounds
                    ? uvec4(imageLoad(historyColors, historyAt) * 255.0 + 0.5) : uvec4(0u);
            }
        }
    }

    if (record.echoStrength > 0u) {
        if (isEdgeStaged(p - record.echoA * frame.faceStep, slot, tileOrigin)) {
            colour = echoBlend(colour, glitchSubjectTint(record.tintA), record.echoStrength);
        }
        if (isEdgeStaged(p - record.echoB * frame.faceStep, slot, tileOrigin)) {
            colour = echoBlend(colour, glitchSubjectTint(record.tintB), record.echoStrength);
        }
    }

    if (record.dissolveThreshold > 0u && all(equal(current, record.id))) {
        int cellSize = record.dissolveCell * frame.faceStep;
        ivec2 cell = ivec2(
            floorDivPositive(relative.x, cellSize),
            floorDivPositive(relative.y, cellSize)
        );
        uint hash = glitchHash(
            frame.seed, frame.tick, kGlitchEffectSubjectDissolve,
            record.salt ^ glitchCellLane(cell)
        );
        if ((hash & 0xFFFFu) < record.dissolveThreshold) {
            colour = uvec4(0u);
        }
    }
    return colour;
}

bool influenceBefore(int slot, int dist, int otherSlot, int otherDist) {
    return dist < otherDist || (dist == otherDist && slot < otherSlot);
}

int stagedSlotAt(ivec2 q, ivec2 tileOrigin) {
    ivec2 local = q - tileOrigin + ivec2(kTileHalo);
    if (any(lessThan(local, ivec2(0))) || any(greaterThanEqual(local, ivec2(kTileWindow)))) {
        return -1;
    }
    return stagedSlots[local.y * kTileWindow + local.x];
}

int stagedSpillAt(ivec2 q, ivec2 tileOrigin) {
    ivec2 local = q - tileOrigin + ivec2(kTileHalo);
    return stagedSpills[local.y * kTileWindow + local.x];
}

uvec4 glitchCell(ivec2 p, ivec2 tileOrigin, PerAxisFrame frame, bool forceScan) {
    int slotA = -1;
    int slotB = -1;
    if (forceScan) {
        int distA = 0;
        int distB = 0;
        int radius = int(subjectWords[1].w) * 2;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius + (dy & 1); dx <= radius; dx += 2) {
                ivec2 sampleAt = p + ivec2(dx, dy);
                int slot = stagedSlotAt(sampleAt, tileOrigin);
                if (slot < 0) {
                    continue;
                }
                int dist = (max(abs(dx), abs(dy)) + 1) >> 1;
                if (dist > stagedSpillAt(sampleAt, tileOrigin)) {
                    continue;
                }
                if (slot == slotA) {
                    distA = min(distA, dist);
                } else if (slot == slotB) {
                    distB = min(distB, dist);
                } else if (slotB < 0 || influenceBefore(slot, dist, slotB, distB)) {
                    slotB = slot;
                    distB = dist;
                }
                if (slotB >= 0 &&
                    (slotA < 0 || influenceBefore(slotB, distB, slotA, distA))) {
                    int swapSlot = slotA;
                    int swapDist = distA;
                    slotA = slotB;
                    distA = distB;
                    slotB = swapSlot;
                    distB = swapDist;
                }
            }
        }
    } else {
        ivec2 local = p - tileOrigin;
        uint levels[5] = uint[5](
            stagedBits[(local.y + kTileHalo) * kTileWindow + local.x + kTileHalo],
            level1[(local.y + 6) * kLevel1Size + local.x + 6],
            level2[(local.y + 4) * kLevel2Size + local.x + 4],
            level3[(local.y + 2) * kLevel3Size + local.x + 2],
            level4[local.y * kLevel4Size + local.x]
        );
        uint seen = 0u;
        for (int distance = 0; distance <= kGlitchSubjectMaxSpill && slotB < 0; ++distance) {
            uint fresh = levels[distance] & ~seen & spillMasks[distance];
            seen |= levels[distance];
            while (fresh != 0u && slotB < 0) {
                int rank = findLSB(fresh);
                fresh &= fresh - 1u;
                if (slotA < 0) {
                    slotA = rankToSlot[rank];
                } else {
                    slotB = rankToSlot[rank];
                }
            }
        }
    }
    if (slotA < 0) {
        return loadColour(p);
    }
    uvec2 idA = subjectWords[recordBase(slotA)].xy;
    if (slotB < 0) {
        return applySubject(p, slotA, idA, tileOrigin, frame);
    }
    uvec2 idB = subjectWords[recordBase(slotB)].xy;
    int lo = min(slotA, slotB);
    int hi = max(slotA, slotB);
    uvec2 idLo = slotA < slotB ? idA : idB;
    uvec2 idHi = slotA < slotB ? idB : idA;
    uint pair = glitchHash(
        frame.seed, frame.tick, kGlitchEffectSubjectPair,
        hash3(hash3(idLo.x, idLo.y, idHi.x), idHi.y, 0u)
    );
    uint mode = pair % kGlitchPairModeCount;
    if (mode == 0u) {
        int row = p.y - frame.storeOrigin.y;
        bool pickHi = (hash3(pair, uint(row), 0u) & 1u) != 0u;
        return applySubject(p, pickHi ? hi : lo, pickHi ? idHi : idLo, tileOrigin, frame);
    }
    if (mode == 1u) {
        return (applySubject(p, lo, idLo, tileOrigin, frame) +
                applySubject(p, hi, idHi, tileOrigin, frame)) >> 1;
    }
    return (applySubject(p, lo, idHi, tileOrigin, frame) +
            applySubject(p, hi, idLo, tileOrigin, frame)) >> 1;
}

void main() {
    PerAxisFrame frame;
    frame.size = imageSize(liveColors);
    uint header = subjectWords[2].x;
    ivec4 current = ivec4(subjectWords[header]);
    ivec4 history = ivec4(subjectWords[header + 1u]);
    frame.storeOrigin = current.xy;
    frame.historyOrigin = history.xy;
    frame.faceStep = current.w;
    frame.depthScale = history.z;
    frame.depthBias = history.w;
    frame.seed = subjectWords[0].x;
    frame.tick = subjectWords[0].yz;
    bool canIndexSubjects = subjectWords[0].w <= uint(kMaxIndexedSubjects);
    if (gl_LocalInvocationIndex < uint(kPresenceWords)) {
        presentWords[gl_LocalInvocationIndex] = 0u;
        rankBases[gl_LocalInvocationIndex] = 0u;
    }
    if (gl_LocalInvocationIndex < uint(kGlitchSubjectMaxSpill + 1)) {
        spillMasks[gl_LocalInvocationIndex] = 0u;
    }
    if (gl_LocalInvocationIndex == 0u) {
        rankedSubjectCount = 0u;
    }
    barrier();
    ivec2 tileGrid = (frame.size + ivec2(kTileSize - 1)) / kTileSize;
    uint tile = tileList[gl_WorkGroupID.x];
    ivec2 tileOrigin = ivec2(int(tile % uint(tileGrid.x)), int(tile / uint(tileGrid.x))) *
                       kTileSize;
    for (uint flatIndex = gl_LocalInvocationIndex;
         flatIndex < uint(kTileWindow * kTileWindow);
         flatIndex += uint(kTileSize * kTileSize)) {
        ivec2 local = ivec2(
            int(flatIndex % uint(kTileWindow)), int(flatIndex / uint(kTileWindow))
        );
        ivec2 q = tileOrigin + local - ivec2(kTileHalo);
        int slot = inCanvas(q, frame.size) ? subjectSlot(loadId(q)) : -1;
        stagedSlots[flatIndex] = slot;
        stagedSpills[flatIndex] = slot >= 0 ? recordSpill(slot) : 0;
        if (canIndexSubjects && slot >= 0) {
            atomicOr(presentWords[uint(slot) >> 5], 1u << (uint(slot) & 31u));
        }
    }
    barrier();
    if (gl_LocalInvocationIndex < uint(kPresenceWords)) {
        uint base = 0u;
        for (uint word = 0u; word < gl_LocalInvocationIndex; ++word) {
            base += bitCount(presentWords[word]);
        }
        rankBases[gl_LocalInvocationIndex] = base;
    }
    barrier();
    if (gl_LocalInvocationIndex == 0u) {
        rankedSubjectCount = rankBases[kPresenceWords - 1] +
                             bitCount(presentWords[kPresenceWords - 1]);
    }
    for (uint slot = gl_LocalInvocationIndex; slot < subjectWords[0].w; slot += 256u) {
        if (!canIndexSubjects) {
            continue;
        }
        uint word = slot >> 5;
        uint bit = 1u << (slot & 31u);
        if ((presentWords[word] & bit) == 0u) {
            continue;
        }
        uint lower = bit - 1u;
        uint rank = rankBases[word] + bitCount(presentWords[word] & lower);
        if (rank >= uint(kMaxRankedSubjects)) {
            continue;
        }
        rankToSlot[rank] = int(slot);
        int spill = recordSpill(int(slot));
        for (int distance = 0; distance <= spill; ++distance) {
            atomicOr(spillMasks[distance], 1u << rank);
        }
    }
    barrier();
    for (uint index = gl_LocalInvocationIndex;
         index < uint(kTileWindow * kTileWindow);
         index += 256u) {
        int slot = stagedSlots[index];
        uint bit = 0u;
        if (canIndexSubjects && rankedSubjectCount <= uint(kMaxRankedSubjects) && slot >= 0) {
            uint word = uint(slot) >> 5;
            uint slotBit = 1u << (uint(slot) & 31u);
            uint rank = rankBases[word] + bitCount(presentWords[word] & (slotBit - 1u));
            bit = 1u << rank;
        }
        stagedBits[index] = bit;
    }
    barrier();
    for (uint index = gl_LocalInvocationIndex;
         index < uint(kLevel1Size * kLevel1Size);
         index += 256u) {
        ivec2 center = ivec2(
            int(index % uint(kLevel1Size)), int(index / uint(kLevel1Size))
        ) + ivec2(4);
        uint mask = 0u;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2 + (dy & 1); dx <= 2; dx += 2) {
                mask |= stagedBits[(center.y + dy) * kTileWindow + center.x + dx];
            }
        }
        level1[index] = mask;
    }
    barrier();
    for (uint index = gl_LocalInvocationIndex;
         index < uint(kLevel2Size * kLevel2Size);
         index += 256u) {
        ivec2 center = ivec2(
            int(index % uint(kLevel2Size)), int(index / uint(kLevel2Size))
        ) + ivec2(2);
        level2[index] = level1[(center.y - 2) * kLevel1Size + center.x - 2] |
                        level1[(center.y - 2) * kLevel1Size + center.x + 2] |
                        level1[(center.y + 2) * kLevel1Size + center.x - 2] |
                        level1[(center.y + 2) * kLevel1Size + center.x + 2];
    }
    barrier();
    for (uint index = gl_LocalInvocationIndex;
         index < uint(kLevel3Size * kLevel3Size);
         index += 256u) {
        ivec2 center = ivec2(
            int(index % uint(kLevel3Size)), int(index / uint(kLevel3Size))
        ) + ivec2(2);
        level3[index] = level2[(center.y - 2) * kLevel2Size + center.x - 2] |
                        level2[(center.y - 2) * kLevel2Size + center.x + 2] |
                        level2[(center.y + 2) * kLevel2Size + center.x - 2] |
                        level2[(center.y + 2) * kLevel2Size + center.x + 2];
    }
    barrier();
    uint level4Index = gl_LocalInvocationIndex;
    ivec2 level4Center = ivec2(
        int(level4Index % uint(kLevel4Size)), int(level4Index / uint(kLevel4Size))
    ) + ivec2(2);
    level4[level4Index] =
        level3[(level4Center.y - 2) * kLevel3Size + level4Center.x - 2] |
        level3[(level4Center.y - 2) * kLevel3Size + level4Center.x + 2] |
        level3[(level4Center.y + 2) * kLevel3Size + level4Center.x - 2] |
        level3[(level4Center.y + 2) * kLevel3Size + level4Center.x + 2];
    barrier();
    ivec2 p = tileOrigin + ivec2(gl_LocalInvocationID.xy);
    if (any(greaterThanEqual(p, frame.size)) || all(equal(loadId(p), uvec2(0u)))) {
        return;
    }
    bool forceScan = subjectWords[2].y != 0u || !canIndexSubjects ||
                     rankedSubjectCount > uint(kMaxRankedSubjects);
    imageStore(scratchColors, p, vec4(glitchCell(p, tileOrigin, frame, forceScan)) / 255.0);
}
