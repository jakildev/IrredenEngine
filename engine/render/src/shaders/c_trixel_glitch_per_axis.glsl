#version 450 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_glitch_common.glsl"
#include "ir_per_axis_cell_dispatch.glsl"

layout(std430, binding = 8) readonly buffer TrixelGlitchSubjects {
    uvec4 subjectWords[];
};
layout(std430, binding = 25) readonly buffer PerAxisCellCompacted {
    uint compactedCells[];
};
layout(std430, binding = 26) readonly buffer PerAxisCellIndirect {
    uint cellDrawArgs[];
};

layout(rgba8, binding = 0) readonly uniform image2D liveColors;
layout(r32i, binding = 1) readonly uniform iimage2D liveDistances;
layout(rg32ui, binding = 2) readonly uniform uimage2D liveEntityIds;
layout(rgba8, binding = 3) readonly uniform image2D historyColors;
layout(rg32ui, binding = 4) readonly uniform uimage2D historyEntityIds;
layout(rgba8, binding = 5) writeonly uniform image2D scratchColors;

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

int slotAt(ivec2 q, PerAxisFrame frame) {
    return inCanvas(q, frame.size) ? subjectSlot(loadId(q)) : -1;
}

bool isEdge(ivec2 q, int slot, PerAxisFrame frame) {
    if (slotAt(q, frame) != slot) {
        return false;
    }
    int step = frame.faceStep;
    return slotAt(q + ivec2(step, 0), frame) != slot ||
           slotAt(q - ivec2(step, 0), frame) != slot ||
           slotAt(q + ivec2(0, step), frame) != slot ||
           slotAt(q - ivec2(0, step), frame) != slot;
}

uvec4 echoBlend(uvec4 colour, uvec4 tint, uint strength) {
    if (colour.a == 0u) {
        return uvec4(tint.rgb, (255u * strength) >> 8);
    }
    uvec4 mixed = (tint * strength + colour * (256u - strength)) >> 8;
    return uvec4(mixed.rgb, colour.a);
}

uvec4 applySubject(ivec2 p, int slot, uvec2 acceptId, PerAxisFrame frame) {
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
        if (isEdge(p - record.echoA * frame.faceStep, slot, frame)) {
            colour = echoBlend(colour, glitchSubjectTint(record.tintA), record.echoStrength);
        }
        if (isEdge(p - record.echoB * frame.faceStep, slot, frame)) {
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

uvec4 glitchCell(ivec2 p, PerAxisFrame frame) {
    int slotA = -1;
    int distA = 0;
    int slotB = -1;
    int distB = 0;
    int radius = int(subjectWords[1].w) * frame.faceStep;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if ((dx + dy) % frame.faceStep != 0) {
                continue;
            }
            int slot = slotAt(p + ivec2(dx, dy), frame);
            if (slot < 0) {
                continue;
            }
            int dist = (max(abs(dx), abs(dy)) + frame.faceStep - 1) / frame.faceStep;
            if (dist > recordSpill(slot)) {
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
            if (slotB >= 0 && (slotA < 0 || influenceBefore(slotB, distB, slotA, distA))) {
                int swapSlot = slotA;
                int swapDist = distA;
                slotA = slotB;
                distA = distB;
                slotB = swapSlot;
                distB = swapDist;
            }
        }
    }
    if (slotA < 0) {
        return loadColour(p);
    }
    uvec2 idA = subjectWords[recordBase(slotA)].xy;
    if (slotB < 0) {
        return applySubject(p, slotA, idA, frame);
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
        return applySubject(p, pickHi ? hi : lo, pickHi ? idHi : idLo, frame);
    }
    if (mode == 1u) {
        return (applySubject(p, lo, idLo, frame) + applySubject(p, hi, idHi, frame)) >> 1;
    }
    return (applySubject(p, lo, idHi, frame) + applySubject(p, hi, idLo, frame)) >> 1;
}

void main() {
    uint idx = perAxisCellInvocationIndex(
        gl_WorkGroupID.x, gl_WorkGroupID.y, gl_NumWorkGroups.x, gl_LocalInvocationIndex
    );
    if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
        return;
    }
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
    ivec2 p = perAxisCellPixel(compactedCells[idx], frame.size.x);
    imageStore(scratchColors, p, vec4(glitchCell(p, frame)) / 255.0);
}
