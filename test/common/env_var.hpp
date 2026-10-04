#ifndef IR_TEST_ENV_VAR_H
#define IR_TEST_ENV_VAR_H

// PURPOSE: Portable environment-variable set / unset for tests that drive an
//   env-read code path on Linux/macOS (setenv/unsetenv) and native Windows
//   (_putenv_s), plus a scope guard that restores the prior value so no case
//   leaks its setting into a sibling or the host shell's view of the run.

#include <cstdlib>
#include <string>

namespace IRTest {

/// Sets @p name to @p value, or unsets it when @p value is nullptr.
inline void setEnvVar(const char *name, const char *value) {
#if defined(_WIN32)
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

class ScopedEnv {
  public:
    ScopedEnv(const char *name, const char *value)
        : m_name(name) {
        if (const char *prior = std::getenv(name)) {
            m_hadPrior = true;
            m_prior = prior;
        }
        setEnvVar(name, value);
    }
    ~ScopedEnv() {
        setEnvVar(m_name.c_str(), m_hadPrior ? m_prior.c_str() : nullptr);
    }
    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

  private:
    std::string m_name;
    bool m_hadPrior = false;
    std::string m_prior;
};

} // namespace IRTest

#endif /* IR_TEST_ENV_VAR_H */
