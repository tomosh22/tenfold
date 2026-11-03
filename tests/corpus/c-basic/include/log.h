#ifndef LOG_H
#define LOG_H

extern int /*@verbosity_decl_log*/g_verbosity;
void log_msg(int level, const char *msg);

#endif
