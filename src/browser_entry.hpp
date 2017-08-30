/* ReaPack: Package manager for REAPER
 * Copyright (C) 2015-2017  Christian Fillion
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

#ifndef REAPACK_BROWSER_ENTRY_HPP
#define REAPACK_BROWSER_ENTRY_HPP

#include "browser.hpp"
#include "listview.hpp"
#include "registry.hpp"

#include <boost/optional.hpp>
#include <memory>

class Index;
class Menu;
class Remote;

typedef std::shared_ptr<const Index> IndexPtr;

class Browser::Entry {
public:
  enum Flag {
    UninstalledFlag = 1<<0,
    InstalledFlag   = 1<<1,
    OutOfDateFlag   = 1<<2,
    ObsoleteFlag    = 1<<3,
  };

  Entry(const Package *, const Registry::Entry &, const IndexPtr &);
  Entry(const Registry::Entry &, const IndexPtr &);

  boost::optional<const Version *> target;
  boost::optional<bool> pin;

  String displayState() const;
  const String &indexName() const;
  const String &categoryName() const;
  const String &packageName() const;
  String displayName() const;
  Package::Type type() const;
  String displayType() const;
  String displayVersion() const;
  const VersionName *sortVersion() const;
  String displayAuthor() const;
  const Time *lastUpdate() const;

  Remote remote() const;
  void updateRow(const ListView::RowPtr &) const;
  void fillMenu(Menu &) const;

  bool test(Flag f) const { return (m_flags & f) != 0; }
  bool canPin() const { return target ? *target != nullptr : test(InstalledFlag); }
  bool operator==(const Entry &o) const;

private:
  int m_flags;

public:
  Registry::Entry regEntry;
  const Package *package;
  IndexPtr index;
  const Version *current;
  const Version *latest;
};

#endif