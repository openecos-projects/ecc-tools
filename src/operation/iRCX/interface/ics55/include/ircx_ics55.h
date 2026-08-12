#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(IRCX_ICS55_BUILD)
#define IRCX_ICS55_API __declspec(dllexport)
#else
#define IRCX_ICS55_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define IRCX_ICS55_API __attribute__((visibility("default")))
#else
#define IRCX_ICS55_API
#endif

IRCX_ICS55_API void ircx_ics55_init(const char* config_file, void* idb_design);
IRCX_ICS55_API void ircx_ics55_run(void);
IRCX_ICS55_API void ircx_ics55_destroy(void);

#ifdef __cplusplus
}
#endif
