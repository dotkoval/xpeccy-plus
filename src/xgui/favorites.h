#pragma once

#include <QWidget>
#include <QString>

// The name and file of one favorite. idx -1 adds path under its own file name,
// anything else edits that entry. True when the list changed; saving it is up
// to the caller
bool fav_edit(QWidget* parent, int idx, const QString& path = QString());
// the whole list - add, edit, reorder, remove - saved when it closes
void fav_manage(QWidget* parent);
