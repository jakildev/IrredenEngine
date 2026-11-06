#include <gtest/gtest.h>

// The GL dispatch table and its loader compile only under the OpenGL backend.
#if defined(IR_GRAPHICS_OPENGL)

#include <irreden/render/gl_wrap/GL.h>

#include <cstring>
#include <set>
#include <string>

namespace {

using GLSlot = void (*)();
static_assert(sizeof(GL4API) % sizeof(GLSlot) == 0, "GL4API must hold only function pointers");
constexpr std::size_t kGLSlotCount = sizeof(GL4API) / sizeof(GLSlot);

std::set<std::string> &requestedNames() {
    static std::set<std::string> names;
    return names;
}

int g_resolvedSentinel = 0;

void *resolveEverything(const char *name) {
    requestedNames().insert(name);
    return &g_resolvedSentinel;
}

// InjectAPITracer4 is deliberately not called: it overwrites the tracer's
// forwarding table that the real GL tests in this binary dispatch through.
GL4API loadEverySlot() {
    requestedNames().clear();
    GL4API api{};
    GetAPI4(&api, resolveEverything);
    return api;
}

TEST(GLApiLoader, EveryDispatchSlotIsRequestedFromTheLoader) {
    const GL4API api = loadEverySlot();

    GLSlot slots[kGLSlotCount];
    std::memcpy(slots, &api, sizeof(api));
    for (std::size_t index = 0; index < kGLSlotCount; ++index) {
        EXPECT_NE(slots[index], nullptr) << "GL4API slot " << index << " is never loaded";
    }
    EXPECT_EQ(requestedNames().size(), kGLSlotCount);
}

// A pointer return type (`const GLubyte *APIENTRY`) shifts the function name
// to a later token than `void APIENTRY` does in the generator's input.
TEST(GLApiLoader, PointerReturningQueriesAreLoaded) {
    const GL4API api = loadEverySlot();

    EXPECT_NE(api.glGetString, nullptr);
    EXPECT_NE(api.glGetStringi, nullptr);
    EXPECT_TRUE(requestedNames().count("glGetString"));
    EXPECT_TRUE(requestedNames().count("glGetStringi"));
}

} // namespace

#endif // IR_GRAPHICS_OPENGL
