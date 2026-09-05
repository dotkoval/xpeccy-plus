#pragma once

#include <QString>

#include <stdio.h>
#include <string>

#include "../libxpeccy/xlog.h"

// The app side of the log: where the file goes, what the header says, and the
// bridge that puts Qt's own messages into the same stream. The core half is
// libxpeccy/xlog.h.

// conf.log.grp[]: the group has not been set apart, it follows conf.log.level
#define XLOG_FOLLOW	(-1)

void log_init();		// as early as main can call it
void log_done();

// parse one argument, the way main's own loop does it: arg is the switch, n
// already points at what follows it and moves on when a value is eaten.
// Returns false when the argument is not ours. Safe to run twice.
bool log_arg(const char* arg, int& n, int ac, char** av);

// open or close the file and push the levels into the core. Called after the
// config is read and again whenever the options change it.
void log_apply();

// Drop what the command line asked for. conf.log then means exactly what the
// config file and the options page hold, which is what log_save writes - so a
// --log meant for one run never turns the log on for good.
void log_args_clear();

// the [LOG] section, both halves in one place
void log_save(FILE* file);
bool log_load(const std::string& key, const char* val, bool flag);

QString log_dir();			// where the log file goes
QString log_file();			// the open one, empty when there is none

// "video:debug,net:off": every group named here is set apart from the base
// level, everything else follows it
void log_groups_set(const QString& spec);
