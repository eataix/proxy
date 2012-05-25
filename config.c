/**************************************************************************
 * config.c - a sectional configuration file parser
 *
 * by RHE, Oct, 2005.
 *
 * This parser is intended to parse configuration files with sections
 * delimited by section headers of the form of [section]. Multiple sections
 * of each section type can be parsed.
 * Within each section, configuration values are of the form:
 *   token = value
 * or
 *   token value
 *
 */

#include <sys/types.h>

#include <ctype.h>
#include <string.h>

#include "config.h"
#include "dbg.h"

#define MAX_CONF_LEN 256

extern int debug_level;

/*
 * allocate a new [section] structure
 */
struct config_sect *
config_new_section(char *name)
{
    struct config_sect *new_sect;

    new_sect = malloc(sizeof(*new_sect));
    if (new_sect) {
        new_sect->name = strdup(name);
        new_sect->tokens = NULL;
        new_sect->next = NULL;
    }
    return new_sect;
}                               /* config_new_section () */

struct config_token *
config_new_token(char *token, char *value)
{
    struct config_token *new_token;

    new_token = malloc(sizeof(*new_token));
    if (new_token) {
        new_token->token = strdup(token);
        new_token->value = strdup(value);
        new_token->next = NULL;
    }
    return new_token;
}                               /* config_new_token () */

/*
 * load a configuration file into memory, allocating structures as required
 */
struct config_sect *
config_load(char *filename)
{
    FILE           *fp;
    char            line[MAX_CONF_LEN];
    struct config_sect *sects = NULL;
    struct config_sect *cur_sect = NULL;
    struct config_token *cur_token = NULL;
    char           *token;
    char           *value;

    fp = fopen(filename, "r");
    check(fp != NULL, "Cannot open the specified configuration file.");

    while (fgets(line, MAX_CONF_LEN, fp) != NULL) {
        /*
         * Skip linear white space at the beginning of the line.
         */
        char           *p;
        for (p = line; *p == ' ' || *p == '\t'; p++);


        if (p != line)
            /**
             * The original implementation uses strcpy(3), which is acceptable
             * in most cases. However, in this case, `s1` and `s2` overlap.
             * According to POSIX.1-2008, "If copying takes place between
             * objects that overlap, the behavior is undefined." Hence, I
             * replace it with memmove(3), which "copy bytes in memory with
             * overlapping areas".
             */
            memmove(line, p, strlen(p) + 1);

        if (line[0] == '#')
            continue;           /* skip lines with comment */
        if (strlen(line) <= 1)
            continue;
        line[strlen(line) - 1] = 0;     /* remove trailing '\n' */
        if (line[0] == '[') {   /* section header */
            token = strtok(line + 1, "]");
            if (token != NULL) {
                struct config_sect *new_sect = config_new_section(token);
                if (new_sect != NULL) {
                    if (cur_sect != NULL) {
                        cur_sect->next = new_sect;
                    } else {
                        sects = new_sect;
                    }
                    cur_sect = new_sect;
                    cur_token = NULL;
                }
            }
        } else {
            token = strtok_r(line, " \t=", &value);
            if (token != NULL) {
                struct config_token *new_token;
                value = strtok(value, " =#\t");
                if (cur_sect == NULL) {
                    cur_sect = config_new_section("default");
                    sects = cur_sect;
                }
                new_token = config_new_token(token, value);
                if (new_token != NULL) {
                    if (cur_token) {
                        cur_token->next = new_token;
                    } else {
                        cur_sect->tokens = new_token;
                    }
                    cur_token = new_token;
                }
            }
        }
    }
    fclose(fp);
    return sects;

error:
    if (fp != NULL)
            fclose(fp);
    return NULL;
}                               /* config_load () */

char           *
config_get_value(struct config_sect *sects, char *section, char *token,
                 int icase)
{
    while (sects != NULL) {
        if ((icase ? strcasecmp(section, sects->name) :
             strcmp(section, sects->name)) == 0) {
            struct config_token *tokens = sects->tokens;
            while (tokens != NULL) {
                if ((icase ? strcasecmp(token, tokens->token) :
                     strcmp(token, tokens->token)) == 0) {
                    return (tokens->value);
                }
                tokens = tokens->next;
            }
        }
        sects = sects->next;
    }
    return NULL;
}                               /* config_get_value () */

void
config_dump(struct config_sect *sects)
{
    struct config_token *tokens;
    while (sects != NULL) {
        printf("[%s]\n", sects->name);
        tokens = sects->tokens;
        while (tokens != NULL) {
            printf("%s\t%s\n", tokens->token, tokens->value);
            tokens = tokens->next;
        }
        sects = sects->next;
    }
}                               /* config_dump () */

void
config_destroy(struct config_sect *sects)
{
    while (sects != NULL) {
        struct config_sect *next = sects->next;
        struct config_token *token = sects->tokens;
        while (token != NULL) {
            struct config_token *next_token = token->next;
            free(memset(token->token, 0, strlen(token->token)));
            free(memset(token->value, 0, strlen(token->value)));
            free(memset(token, 0, sizeof(struct config_token)));
            token = next_token;
        }
        free(memset(sects->name, 0, strlen(sects->name)));
        free(memset(sects, 0, sizeof(struct config_sect)));
        sects = next;
    }
}                               /* config_destroy () */
