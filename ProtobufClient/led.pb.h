/* Automatically generated nanopb header */
/* Compatible with nanopb-0.4.9 */

#ifndef LED_PB_H_INCLUDED
#define LED_PB_H_INCLUDED

#include "pb.h"

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Struct definitions */
typedef struct _LedCommand {
    bool state;
} LedCommand;

/* Initializer values */
#define LedCommand_init_default   {false}
#define LedCommand_init_zero      {false}

/* Field tags (optional but praktisch) */
#define LedCommand_state_tag      1
#define LedCommand_DEFAULT        NULL
#define LedCommand_CALLBACK       NULL

/* Field encoding specification */
#define LedCommand_FIELDLIST(X, a) \
X(a, STATIC, REQUIRED, BOOL, state, 1)

extern const pb_msgdesc_t LedCommand_msg;

#define LedCommand_fields &LedCommand_msg

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif

