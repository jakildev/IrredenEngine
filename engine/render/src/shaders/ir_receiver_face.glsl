#ifndef IR_RECEIVER_FACE
#define IR_RECEIVER_FACE

// Zero is the legacy receiver; the six axis normals occupy exact UNORM8 codes.
float encodeReceiverFace(vec3 normal) {
    int axis = normal.x != 0.0 ? 0 : (normal.y != 0.0 ? 1 : 2);
    int face = axis * 2 + (normal[axis] > 0.0 ? 1 : 0);
    return float(face + 1) / 255.0;
}

vec3 receiverFaceNormal(float encoded, vec3 fallbackNormal) {
    int face = int(floor(encoded * 255.0 + 0.5)) - 1;
    return face >= 0 ? faceOutwardNormal6(face) : fallbackNormal;
}

#endif
