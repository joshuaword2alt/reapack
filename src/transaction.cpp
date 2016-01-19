/* ReaPack: Package manager for REAPER
 * Copyright (C) 2015-2016  Christian Fillion
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "transaction.hpp"

#include "encoding.hpp"
#include "errors.hpp"
#include "index.hpp"
#include "remote.hpp"
#include "task.hpp"

#include <fstream>

#include <reaper_plugin_functions.h>

using namespace std;

Transaction::Transaction(const Path &root)
  : m_root(root), m_step(Unknown), m_isCancelled(false), m_hasConflicts(false)
{
  m_dbPath = m_root + "ReaPack";

  m_registry = new Registry(m_dbPath + "registry.db");

  m_queue.onDone([=](void *) {
    switch(m_step) {
    case Synchronize:
      updateAll();
      break;
    default:
      finish();
      break;
    }
  });

  RecursiveCreateDirectory(m_dbPath.join().c_str(), 0);
}

Transaction::~Transaction()
{
  for(Task *task : m_tasks)
    delete task;

  for(RemoteIndex *ri : m_remoteIndexes)
    delete ri;

  delete m_registry;
}

void Transaction::synchronize(const Remote &remote)
{
  m_step = Synchronize;

  Download *dl = new Download(remote.name(), remote.url());
  dl->onFinish(bind(&Transaction::saveRemoteIndex, this, dl));

  m_queue.push(dl);
}

void Transaction::saveRemoteIndex(Download *dl)
{
  const Path path = m_dbPath + ("remote_" + dl->name() + ".xml");

  if(!saveFile(dl, path))
    return;

  try {
    RemoteIndex *ri = RemoteIndex::load(dl->name(), path.join().c_str());
    m_remoteIndexes.push_back(ri);
  }
  catch(const reapack_error &e) {
    addError(e.what(), dl->url());
  }
}

void Transaction::updateAll()
{
  for(RemoteIndex *ri : m_remoteIndexes) {
    for(Package *pkg : ri->packages()) {
      Registry::Entry entry = m_registry->query(pkg);

      Version *ver = pkg->lastVersion();

      set<Path> files = ver->files();
      registerFiles(files);

      if(entry.status == Registry::UpToDate) {
        if(allFilesExists(files))
          continue;
        else
          entry.status = Registry::Uninstalled;
      }

      m_packages.push_back({ver, entry});
    }
  }

  if(m_packages.empty() || m_hasConflicts)
    finish();
  else
    install();
}

void Transaction::install()
{
  m_step = Install;

  for(const PackageEntry &entry : m_packages) {
    Version *ver = entry.first;
    const Registry::Entry regEntry = entry.second;
    const set<Path> &currentFiles = m_registry->getFiles(regEntry);

    InstallTask *task = new InstallTask(ver, currentFiles, this);

    task->onCommit([=] {
      if(regEntry.status == Registry::UpdateAvailable)
        m_updates.push_back(entry);
      else
        m_new.push_back(entry);

      m_registry->push(ver);

      if(!m_registry->addToREAPER(ver, m_root)) {
        addError(
          "Cannot register the package in REAPER. "
          "Are you using REAPER v5.12 or more recent?", ver->fullName()
        );
      }
    });

    addTask(task);
  }
}

void Transaction::uninstall(const Remote &remote)
{
  const vector<Registry::Entry> &entries = m_registry->queryAll(remote);

  if(entries.empty()) {
    cancel();
    return;
  }

  for(const auto &entry : entries) {
    const set<Path> &files = m_registry->getFiles(entry);

    RemoveTask *task = new RemoveTask(files, this);

    task->onCommit([=] {
      const vector<Path> &removedFiles = task->removedFiles();

      m_registry->forget(entry);

      m_removals.insert(m_removals.end(),
        removedFiles.begin(), removedFiles.end());
    });

    addTask(task);
  }
}

void Transaction::cancel()
{
  m_isCancelled = true;

  for(Task *task : m_tasks)
    task->rollback();

  if(m_queue.idle())
    finish();
  else
    m_queue.abort();
}

bool Transaction::saveFile(Download *dl, const Path &path)
{
  if(dl->status() != 200) {
    addError(dl->contents(), dl->url());
    return false;
  }

  RecursiveCreateDirectory(path.dirname().c_str(), 0);

  const string strPath = path.join();
  ofstream file(make_autostring(strPath), ios_base::binary);

  if(!file) {
    addError(strerror(errno), strPath);
    return false;
  }

  file << dl->contents();
  file.close();

  return true;
}

void Transaction::finish()
{
  // called when the download queue is done, or if there is nothing to do

  if(!m_isCancelled) {
    for(Task *task : m_tasks)
      task->commit();

    m_registry->commit();
  }

  m_onFinish();
  m_onDestroy();
}

void Transaction::addError(const string &message, const string &title)
{
  m_errors.push_back({message, title});
}

Path Transaction::prefixPath(const Path &input) const
{
  return m_root + input;
}

bool Transaction::allFilesExists(const set<Path> &list) const
{
  for(const Path &path : list) {
    if(!file_exists(prefixPath(path).join().c_str()))
      return false;
  }

  return true;
}

void Transaction::registerFiles(const set<Path> &list)
{
  for(const Path &path : list) {
    if(!m_files.count(path))
      continue;

    addError("Conflict: This file is owned by more than one package",
      path.join());

    m_hasConflicts = true;
  }

  m_files.insert(list.begin(), list.end());
}

void Transaction::addTask(Task *task)
{
  m_tasks.push_back(task);

  if(m_queue.idle())
    finish();
}
