#ifndef SEYRIX_SHELL_H
#define SEYRIX_SHELL_H

#include "seyrix.h"

bool seyrix_shell_init(struct seyrix_server *server);
bool seyrix_shell_start(struct seyrix_server *server);
void seyrix_shell_child_exited(struct seyrix_server *server, pid_t pid);
void seyrix_shell_finish(struct seyrix_server *server);
void seyrix_shell_outputs_changed(struct seyrix_server *server);
void seyrix_shell_update_active_workspace(struct seyrix_server *server);

#endif
