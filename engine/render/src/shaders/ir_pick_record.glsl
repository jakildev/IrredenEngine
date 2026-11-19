struct PickRecord {
    uvec2 entityId;
    float depth;
    float pad;
    uvec2 scatterEntityId;
    float scatterDepth;
    uint scatterOwner;
};

void gatherPick(inout PickRecord pick, uvec2 id, float depth, float alpha) {
    if (alpha >= 0.1 && depth <= pick.depth && any(notEqual(id, uvec2(0)))) {
        pick.entityId = id;
        pick.depth = depth;
    }
}

void scatterPick(inout PickRecord pick, uvec2 id, float depth, int mode) {
    if (mode == 0) {
        pick.scatterOwner = any(notEqual(id, uvec2(0))) ? 1u : 0u;
        pick.scatterEntityId = id;
        pick.scatterDepth = depth;
    } else if (pick.scatterOwner != 0u &&
               floatBitsToUint(depth) == floatBitsToUint(pick.scatterDepth)) {
        pick.entityId = pick.scatterEntityId;
        pick.depth = depth;
    }
}
