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

#include "index.hpp"

#include "encoding.hpp"
#include "errors.hpp"
#include "path.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <cerrno>

#include <WDL/tinyxml/tinyxml.h>

using namespace std;

static FILE *OpenFile(const char *path)
{
#ifdef _WIN32
  FILE *file = nullptr;
  _wfopen_s(&file, make_autostring(path).c_str(), L"rb");
  return file;
#else
  return fopen(path, "rb");
#endif
}

Path RemoteIndex::pathFor(const string &name)
{
  return Path::prefixCache(name + ".xml");
}

auto RemoteIndex::linkTypeFor(const char *rel) -> LinkType
{
  if(!strcmp(rel, "donation"))
    return DonationLink;

  return WebsiteLink;
}

const RemoteIndex *RemoteIndex::load(const string &name)
{
  TiXmlDocument doc;

  FILE *file = OpenFile(pathFor(name).join().c_str());

  if(!file)
    throw reapack_error(strerror(errno));

  const bool success = doc.LoadFile(file);
  fclose(file);

  if(!success)
    throw reapack_error(doc.ErrorDesc());

  TiXmlHandle docHandle(&doc);
  TiXmlElement *root = doc.RootElement();

  if(strcmp(root->Value(), "index"))
    throw reapack_error("invalid index");

  int version = 0;
  root->Attribute("version", &version);

  if(!version)
    throw reapack_error("invalid version");

  switch(version) {
  case 1:
    return loadV1(root, name);
  default:
    throw reapack_error("unsupported version");
  }
}

RemoteIndex::RemoteIndex(const string &name)
  : m_name(name)
{
  if(m_name.empty())
    throw reapack_error("empty index name");
}

RemoteIndex::~RemoteIndex()
{
  for(const Category *cat : m_categories)
    delete cat;
}

void RemoteIndex::addCategory(const Category *cat)
{
  if(cat->index() != this)
    throw reapack_error("category belongs to another index");

  if(cat->packages().empty())
    return;

  m_categories.push_back(cat);

  m_packages.insert(m_packages.end(),
    cat->packages().begin(), cat->packages().end());
}

void RemoteIndex::addLink(const LinkType type, const Link &link)
{
  if(boost::algorithm::starts_with(link.url, "http"))
    m_links.insert({type, link});
}

auto RemoteIndex::links(const LinkType type) const -> LinkList
{
  const auto begin = m_links.lower_bound(type);
  const auto end = m_links.upper_bound(type);

  LinkList list(m_links.count(type));

  for(auto it = begin; it != end; it++)
    list[distance(begin, it)] = &it->second;

  return list;
}

Category::Category(const string &name, const RemoteIndex *ri)
  : m_index(ri), m_name(name)
{
  if(m_name.empty())
    throw reapack_error("empty category name");
}

Category::~Category()
{
  for(const Package *pack : m_packages)
    delete pack;
}

string Category::fullName() const
{
  return m_index ? m_index->name() + "/" + m_name : m_name;
}

void Category::addPackage(const Package *pkg)
{
  if(pkg->category() != this)
    throw reapack_error("package belongs to another category");

  if(pkg->type() == Package::UnknownType)
    return; // silently discard unknown package types
  else if(pkg->versions().empty())
    return;

  m_packages.push_back(pkg);
}
