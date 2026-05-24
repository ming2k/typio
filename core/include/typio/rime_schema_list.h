/**
 * @file rime_schema_list.h
 * @brief Shared helpers for discovering available Rime schemas.
 */

#ifndef TYPIO_RIME_SCHEMA_LIST_H
#define TYPIO_RIME_SCHEMA_LIST_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TYPIO_RIME_SCHEMA_LIST_MAX_SCHEMAS 32

typedef struct TypioRimeSchemaInfo {
    char *id;
    char *name;
} TypioRimeSchemaInfo;

typedef struct TypioRimeSchemaList {
    bool available;
    char *current_schema;
    char *user_data_dir;
    TypioRimeSchemaInfo schemas[TYPIO_RIME_SCHEMA_LIST_MAX_SCHEMAS];
    size_t schema_count;
} TypioRimeSchemaList;

bool typio_rime_schema_list_load(const TypioConfig *config,
                                 const char *default_data_dir,
                                 TypioRimeSchemaList *list);

void typio_rime_schema_list_clear(TypioRimeSchemaList *list);

#ifdef __cplusplus
}
#endif

#endif
