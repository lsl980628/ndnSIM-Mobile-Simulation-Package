/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2011 University of California, Los Angeles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Alexander Afanasyev <alexander.afanasyev@ucla.edu>
 */

#include "ccnx-pit-impl.h"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/simulator.h"
#include "ccnx-interest-header.h"
#include "ccnx-content-object-header.h"

#include <boost/lambda/bind.hpp>
#include <boost/lambda/lambda.hpp>

NS_LOG_COMPONENT_DEFINE ("CcnxPitImpl");

using namespace boost::tuples;
using namespace boost;
namespace ll = boost::lambda;

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (CcnxPitImpl);

TypeId
CcnxPitImpl::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::CcnxPit")
    .SetGroupName ("Ccnx")
    .SetParent<CcnxPit> ()
    .AddConstructor<CcnxPitImpl> ()
    .AddAttribute ("MaxSize",
                   "Set maximum number of entries in PIT. If 0, limit is not enforced",
                   StringValue ("0"),
                   MakeUintegerAccessor (&CcnxPitImpl::GetMaxSize, &CcnxPitImpl::SetMaxSize),
                   MakeUintegerChecker<uint32_t> ())
    ;

  return tid;
}

CcnxPitImpl::CcnxPitImpl ()
{
}

CcnxPitImpl::~CcnxPitImpl ()
{
}

uint32_t
CcnxPitImpl::GetMaxSize () const
{
  return getPolicy ().get_max_size ();
}

void
CcnxPitImpl::SetMaxSize (uint32_t maxSize)
{
  getPolicy ().set_max_size (maxSize);
}

void 
CcnxPitImpl::NotifyNewAggregate ()
{
  if (m_fib == 0)
    {
      m_fib = GetObject<CcnxFib> ();
    }
}

void 
CcnxPitImpl::DoDispose ()
{
  clear ();
}

void
CcnxPitImpl::DoCleanExpired ()
{
  // NS_LOG_LOGIC ("Cleaning PIT. Total: " << size ());
  Time now = Simulator::Now ();

  NS_LOG_ERROR ("Need to be repaired");
  // // uint32_t count = 0;
  // while (!empty ())
  //   {
  //     CcnxPit::index<i_timestamp>::type::iterator entry = get<i_timestamp> ().begin ();
  //     if (entry->GetExpireTime () <= now) // is the record stale?
  //       {
  //         get<i_timestamp> ().erase (entry);
  //         // count ++;
  //       }
  //     else
  //       break; // nothing else to do. All later records will not be stale
  //   }
}

Ptr<CcnxPitEntry>
CcnxPitImpl::Lookup (const CcnxContentObjectHeader &header)
{
  /// @todo use predicate to search with exclude filters  
  super::iterator item = super::longest_prefix_match (header.GetName ());

  if (item == super::end ())
    return 0;
  else
    return item->payload (); // which could also be 0
}

Ptr<CcnxPitEntry>
CcnxPitImpl::Lookup (const CcnxInterestHeader &header)
{
  NS_LOG_FUNCTION (header.GetName ());
  NS_ASSERT_MSG (m_fib != 0, "FIB should be set");

  super::iterator foundItem, lastItem;
  bool reachLast;
  boost::tie (foundItem, reachLast, lastItem) = super::getTrie ().find (header.GetName ());

  if (!reachLast || lastItem == super::end ())
    return 0;
  else
    return lastItem->payload (); // which could also be 0
}

Ptr<CcnxPitEntry>
CcnxPitImpl::Create (Ptr<const CcnxInterestHeader> header)
{
  Ptr<CcnxFibEntry> fibEntry = m_fib->LongestPrefixMatch (*header);
  NS_ASSERT_MSG (fibEntry != 0,
                 "There should be at least default route set" <<
                 " Prefix = "<< header->GetName() << "NodeID == " << m_fib->GetObject<Node>()->GetId() << "\n" << *m_fib);


  Ptr<CcnxPitEntryImpl> newEntry = ns3::Create<CcnxPitEntryImpl> (header, fibEntry);
  std::pair< super::iterator, bool > result = super::insert (header->GetName (), newEntry);
  if (result.first != super::end ())
    {
      if (result.second)
        {
          newEntry->SetTrie (result.first);
          return newEntry;
        }
      else
        {
          // should we do anything?
          // update payload? add new payload?
          return result.first->payload ();
        }
    }
  else
    return 0;
}


void
CcnxPitImpl::MarkErased (Ptr<CcnxPitEntry> entry)
{
  // entry->SetExpireTime (Simulator::Now () + m_PitEntryPruningTimout);
  super::erase (StaticCast<CcnxPitEntryImpl> (entry)->to_iterator ());
}


void
CcnxPitImpl::Print (std::ostream& os) const
{
  // !!! unordered_set imposes "random" order of item in the same level !!!
  super::parent_trie::const_recursive_iterator item (super::getTrie ()), end (0);
  for (; item != end; item++)
    {
      if (item->payload () == 0) continue;

      os << item->payload ()->GetPrefix () << "\t" << *item->payload () << "\n";
    }
}

Ptr<CcnxPitEntry>
CcnxPitImpl::Begin ()
{
  super::parent_trie::recursive_iterator item (super::getTrie ()), end (0);
  for (; item != end; item++)
    {
      if (item->payload () == 0) continue;
      break;
    }

  if (item == end)
    return End ();
  else
    return item->payload ();
}

Ptr<CcnxPitEntry>
CcnxPitImpl::End ()
{
  return 0;
}

Ptr<CcnxPitEntry>
CcnxPitImpl::Next (Ptr<CcnxPitEntry> from)
{
  if (from == 0) return 0;
  
  super::parent_trie::recursive_iterator
    item (*StaticCast<CcnxPitEntryImpl> (from)->to_iterator ()),
    end (0);
  
  for (item++; item != end; item++)
    {
      if (item->payload () == 0) continue;
      break;
    }

  if (item == end)
    return End ();
  else
    return item->payload ();
}


} // namespace ns3