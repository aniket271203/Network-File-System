#ifndef __UTILS_H
#define __UTILS_H

#include "headers.h"

int connectViaTCP(char IP[], int port, struct sockaddr_in* serverInfo);

/* NM - SS comms utils*/
void print_ss_info(ss_info *info);
char *stringifySSInfo(ss_info *info);    // call this in SS, returns null-terminated string
ss_info *destringifySSInfo(char *info); // call this in NM
char *stringifyAddedPaths(added_paths *ap, int numTotalPaths);

/* functions for path resolution */
char *getRelPath(const char *absPath, const char *ref);
char *getAbsPath(const char *relPath, const char *ref);

#endif //__UTILS_H
