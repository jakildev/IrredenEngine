int sdfRayQuadraticRoots(float a, float b, float c, out vec2 roots) {
    if (a == 0.0) {
        if (b == 0.0) return 0;
        roots = vec2(-c / b);
        return 1;
    }
    float discriminant = b * b - 4.0 * a * c;
    float roundingTolerance = 4.0e-7 * (b * b + abs(4.0 * a * c));
    if (discriminant < -roundingTolerance) return 0;
    if (discriminant <= 0.0) {
        roots = vec2(-b / (2.0 * a));
        return 1;
    }
    float radical = sqrt(discriminant);
    float q = -0.5 * (b + (b < 0.0 ? -radical : radical));
    if (q == 0.0) {
        roots = vec2(-b / (2.0 * a));
        return 1;
    }
    float first = q / a;
    float second = c / q;
    roots = vec2(min(first, second), max(first, second));
    return 2;
}

bool sdfRaySlab(float origin, float direction, float minimum, float maximum,
                inout float nearDepth, inout float farDepth) {
    if (direction == 0.0) return origin >= minimum && origin <= maximum;
    float a = (minimum - origin) / direction;
    float b = (maximum - origin) / direction;
    nearDepth = max(nearDepth, min(a, b));
    farDepth = min(farDepth, max(a, b));
    return nearDepth <= farDepth;
}

bool sdfRaySphere(vec3 origin, vec3 direction, float radius,
                  float nearDepth, float farDepth, out float depth) {
    vec3 start = origin + direction * nearDepth;
    if (dot(start, start) <= radius * radius) {
        depth = nearDepth;
        return true;
    }
    float projection = dot(start, direction);
    vec3 closest = start - direction * projection;
    float chordSquared = radius * radius - dot(closest, closest);
    if (chordSquared < -4.0e-7 * radius * radius) return false;
    depth = nearDepth - projection - sqrt(max(chordSquared, 0.0));
    return depth >= nearDepth && depth <= farDepth;
}

// Coefficients use distance from nearDepth as their polynomial variable.
bool sdfRayQuadraticInterval(float a, float b, float c,
                             float nearDepth, float farDepth, out float depth) {
    if (c <= 0.0) {
        depth = nearDepth;
        return true;
    }
    vec2 roots;
    int count = sdfRayQuadraticRoots(a, b, c, roots);
    for (int index = 0; index < count; ++index) {
        float candidate = nearDepth + roots[index];
        if (candidate >= nearDepth && candidate <= farDepth) {
            depth = candidate;
            return true;
        }
    }
    return false;
}

bool sdfRayCylinder(vec3 origin, vec3 direction, float radius,
                    float minimumZ, float maximumZ,
                    float nearDepth, float farDepth, out float depth) {
    if (!sdfRaySlab(origin.z, direction.z, minimumZ, maximumZ, nearDepth, farDepth))
        return false;
    vec3 start = origin + direction * nearDepth;
    return sdfRayQuadraticInterval(dot(direction.xy, direction.xy),
        2.0 * dot(start.xy, direction.xy), dot(start.xy, start.xy) - radius * radius,
        nearDepth, farDepth, depth);
}

float sdfRayCubicValue(vec4 coefficients, float x) {
    return ((coefficients.x * x + coefficients.y) * x + coefficients.z) * x + coefficients.w;
}

// The quadratic derivative partitions the cubic into monotone intervals.
// Every real root in [-1,1], including repeated roots, is an interval boundary
// or the sole sign crossing in an interval.
int sdfRayCubicRoots(vec4 coefficients, out vec3 roots) {
    vec2 critical;
    int criticalCount = sdfRayQuadraticRoots(3.0 * coefficients.x,
        2.0 * coefficients.y, coefficients.z, critical);
    float boundaries[4];
    int boundaryCount = 1;
    boundaries[0] = -1.0;
    for (int index = 0; index < criticalCount; ++index) {
        if (critical[index] > -1.0 && critical[index] < 1.0)
            boundaries[boundaryCount++] = critical[index];
    }
    boundaries[boundaryCount++] = 1.0;
    int count = 0;
    for (int index = 0; index < boundaryCount - 1; ++index) {
        float left = boundaries[index];
        float right = boundaries[index + 1];
        float leftValue = sdfRayCubicValue(coefficients, left);
        float rightValue = sdfRayCubicValue(coefficients, right);
        if (leftValue == 0.0 && count < 3) roots[count++] = left;
        if ((leftValue < 0.0 && rightValue > 0.0) ||
            (leftValue > 0.0 && rightValue < 0.0)) {
            for (int iteration = 0; iteration < 24; ++iteration) {
                float middle = 0.5 * (left + right);
                float value = sdfRayCubicValue(coefficients, middle);
                if ((leftValue < 0.0) == (value < 0.0)) {
                    left = middle;
                    leftValue = value;
                } else right = middle;
            }
            if (count < 3) roots[count++] = 0.5 * (left + right);
        }
    }
    if (sdfRayCubicValue(coefficients, 1.0) == 0.0 && count < 3) roots[count++] = 1.0;
    return count;
}

float sdfRayTorusValue(vec3 point, float majorRadius, float minorRadius) {
    float radial = length(point.xy) - majorRadius;
    return radial * radial + point.z * point.z - minorRadius * minorRadius;
}

bool sdfRayTorus(vec3 origin, vec3 direction, float majorRadius, float minorRadius,
                 float nearDepth, float farDepth, out float depth) {
    // A unit interval and shape-relative coordinates condition the derivative;
    // geometric evaluation avoids subtracting quartic terms at thin tube rims.
    float scale = max(majorRadius + minorRadius, 1.0e-6);
    float middleDepth = 0.5 * (nearDepth + farDepth);
    float halfRange = 0.5 * (farDepth - nearDepth);
    vec3 rayCenter = origin + direction * middleDepth;
    vec3 rayDelta = direction * halfRange;
    vec3 center = rayCenter / scale;
    vec3 delta = rayDelta / scale;
    float major = majorRadius / scale;
    float minor = minorRadius / scale;
    float a = dot(delta, delta);
    float b = 2.0 * dot(center, delta);
    float c = dot(center, center) + major * major - minor * minor;
    float radialA = dot(delta.xy, delta.xy);
    float radialB = 2.0 * dot(center.xy, delta.xy);
    vec4 derivative = vec4(4.0 * a * a, 6.0 * a * b,
        2.0 * (b * b + 2.0 * a * c - 4.0 * major * major * radialA),
        2.0 * b * c - 4.0 * major * major * radialB);
    vec3 critical;
    int criticalCount = sdfRayCubicRoots(derivative, critical);
    float left = -1.0;
    float leftValue = sdfRayTorusValue(rayCenter - rayDelta, majorRadius, minorRadius);
    if (leftValue <= 0.0) {
        depth = nearDepth;
        return true;
    }
    for (int index = 0; index <= criticalCount; ++index) {
        float right = index < criticalCount ? critical[index] : 1.0;
        float rightValue = sdfRayTorusValue(rayCenter + rayDelta * right, majorRadius, minorRadius);
        // Tangencies have no sign crossing. Their residual is limited by the
        // float operations on the radii, rather than a ray-march hit tolerance.
        float roundingTolerance = 4.0e-7 * (majorRadius + minorRadius) * minorRadius;
        if (rightValue >= 0.0 && rightValue <= roundingTolerance) {
            depth = middleDepth + halfRange * right;
            return true;
        }
        if (rightValue <= 0.0) {
            for (int iteration = 0; iteration < 24; ++iteration) {
                float midpoint = 0.5 * (left + right);
                float value = sdfRayTorusValue(rayCenter + rayDelta * midpoint, majorRadius, minorRadius);
                if (value > 0.0) left = midpoint;
                else right = midpoint;
            }
            depth = middleDepth + halfRange * right;
            return true;
        }
        left = right;
    }
    return false;
}

bool sdfRayConeBody(vec3 origin, vec3 direction, float radius, float halfHeight,
                    float threshold, float nearDepth, float farDepth, out float depth) {
    if (!sdfRaySlab(origin.z, direction.z, -halfHeight, halfHeight, nearDepth, farDepth))
        return false;
    vec3 start = origin + direction * nearDepth;
    float slope = radius / (2.0 * halfHeight);
    float startRadius = radius * 0.5 + threshold - slope * start.z;
    float radiusDelta = -slope * direction.z;
    return sdfRayQuadraticInterval(
        dot(direction.xy, direction.xy) - radiusDelta * radiusDelta,
        2.0 * (dot(start.xy, direction.xy) - startRadius * radiusDelta),
        dot(start.xy, start.xy) - startRadius * startRadius,
        nearDepth, farDepth, depth);
}

// The direction is unit length; the interval is the caller's shape AABB clip.
// These solids match evaluateSDF <= threshold, including its rounded caps.
bool sdfRayHit(uint shapeType, vec4 params, vec3 origin, vec3 direction,
               float threshold, float nearDepth, float farDepth, out float depth) {
    if (nearDepth > farDepth) return false;
    if (shapeType == SHAPE_SPHERE)
        return sdfRaySphere(origin, direction, params.x + threshold, nearDepth, farDepth, depth);
    if (shapeType == SHAPE_TORUS)
        return sdfRayTorus(origin, direction, params.x, params.y + threshold, nearDepth, farDepth, depth);
    float halfHeight = 0.5 * params.z;
    float candidate;
    bool hit = false;
    depth = farDepth;
    if (shapeType == SHAPE_CYLINDER) {
        if (sdfRayCylinder(origin, direction, params.x + threshold, -halfHeight, halfHeight,
                           nearDepth, depth, candidate)) { depth = candidate; hit = true; }
        if (sdfRayCylinder(origin, direction, params.x, -halfHeight - threshold, halfHeight + threshold,
                           nearDepth, depth, candidate)) { depth = candidate; hit = true; }
        if (threshold == 0.0) return hit;
        for (int cap = 0; cap < 2; ++cap) {
            float capHeight = cap == 0 ? -halfHeight : halfHeight;
            float capNear = nearDepth;
            float capFar = depth;
            float lower = cap == 0 ? capHeight - threshold : capHeight;
            float upper = cap == 0 ? capHeight : capHeight + threshold;
            if (sdfRaySlab(origin.z, direction.z, lower, upper, capNear, capFar) &&
                sdfRayTorus(origin - vec3(0.0, 0.0, capHeight), direction, params.x, threshold,
                             capNear, capFar, candidate))
                { depth = candidate; hit = true; }
        }
        return hit;
    }
    if (shapeType != SHAPE_CONE) return false;
    if (sdfRayConeBody(origin, direction, params.x, halfHeight, threshold, nearDepth, depth, candidate))
        { depth = candidate; hit = true; }
    if (sdfRaySphere(origin - vec3(0.0, 0.0, halfHeight), direction, threshold,
                     nearDepth, depth, candidate)) { depth = candidate; hit = true; }
    if (sdfRayCylinder(origin, direction, params.x, -halfHeight - threshold, -halfHeight,
                       nearDepth, depth, candidate)) { depth = candidate; hit = true; }
    if (threshold == 0.0) return hit;
    float capNear = nearDepth;
    float capFar = depth;
    if (sdfRaySlab(origin.z, direction.z, -halfHeight - threshold, -halfHeight, capNear, capFar) &&
        sdfRayTorus(origin + vec3(0.0, 0.0, halfHeight), direction, params.x, threshold,
                     capNear, capFar, candidate)) { depth = candidate; hit = true; }
    return hit;
}
