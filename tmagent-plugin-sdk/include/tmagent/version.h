#ifndef TMAGENT_VERSION_H
#define TMAGENT_VERSION_H

// TmAgent Plugin SDK Version Information
// Follows Semantic Versioning (MAJOR.MINOR.PATCH)

#define TMAGENT_SDK_VERSION_MAJOR 1
#define TMAGENT_SDK_VERSION_MINOR 0
#define TMAGENT_SDK_VERSION_PATCH 0

#define TMAGENT_SDK_VERSION_STRING "1.0.0"

// Version comparison macros
#define TMAGENT_SDK_VERSION_CHECK(major, minor, patch) \
    ((major << 16) | (minor << 8) | (patch))

#define TMAGENT_SDK_VERSION \
    TMAGENT_SDK_VERSION_CHECK(TMAGENT_SDK_VERSION_MAJOR, \
                              TMAGENT_SDK_VERSION_MINOR, \
                              TMAGENT_SDK_VERSION_PATCH)

#endif // TMAGENT_VERSION_H
