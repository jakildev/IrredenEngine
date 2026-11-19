struct PickRecord {
    uint2 entityId;
    float depth;
    float pad;
    uint2 scatterEntityId;
    float scatterDepth;
    uint scatterOwner;
};

static void gatherPick(device PickRecord &pick, uint2 id, float depth, float alpha) {
    if (alpha >= 0.1f && depth <= pick.depth && any(id != uint2(0))) {
        pick.entityId = id;
        pick.depth = depth;
    }
}

static void scatterPick(device PickRecord &pick, uint2 id, float depth, int mode) {
    if (mode == 0) {
        pick.scatterOwner = any(id != uint2(0)) ? 1u : 0u;
        pick.scatterEntityId = id;
        pick.scatterDepth = depth;
    } else if (pick.scatterOwner != 0u && as_type<uint>(depth) == as_type<uint>(pick.scatterDepth)) {
        pick.entityId = pick.scatterEntityId;
        pick.depth = depth;
    }
}
