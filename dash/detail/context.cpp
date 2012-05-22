
/* Copyright (c) 2011-2012, EFPL/Blue Brain Project
 *                     Stefan Eilemann <stefan.eilemann@epfl.ch> 
 *
 * This file is part of DASH <https://github.com/BlueBrain/dash>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3.0 as published
 * by the Free Software Foundation.
 *  
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "context.h"

#include "attribute.h"
#include "change.h"
#include "commit.h"
#include "node.h"

#include <dash/context.h>
#include <dash/node.h>
#include <lunchbox/atomic.h>

namespace dash
{
namespace detail
{

namespace
{
lunchbox::a_int32_t numSlots_;
LFVector< int32_t > freeSlots_;

static int32_t allocSlot_()
{
    int32_t slot = 0;
    if( freeSlots_.pop_back( slot ))
        return slot;
    return ((++numSlots_) - 1);
}

class MapVisitor : public dash::Node::Visitor
{
public:
    MapVisitor( const Context& from, const Context& to )
            : from_( from ), to_( to ) {}
    virtual ~MapVisitor() {}

    virtual VisitorResult visitDown( dash::NodePtr node )
        {
            node->getImpl()->map( from_, to_ );
            return ACCEPT_CONTINUE;
        }

    virtual VisitorResult visit( dash::AttributePtr attribute )
        {
            attribute->getImpl()->map( from_, to_ );
            return ACCEPT_CONTINUE;
        }

private:
    const Context& from_;
    const Context& to_;
};

class UnmapVisitor : public dash::Node::Visitor
{
public:
    UnmapVisitor( Context& context ) : context_( context ) {}
    virtual ~UnmapVisitor() {}

    virtual VisitorResult visitUp( dash::AttributePtr attribute )
        {
            attribute->getImpl()->unmap( context_ );
            return ACCEPT_CONTINUE;
        }

    virtual VisitorResult visitUp( dash::NodePtr node )
        {
            node->getImpl()->unmap( context_ );
            return ACCEPT_CONTINUE;
        }

private:
    Context& context_;
};

}

Context::Context()
        : slot_( allocSlot_( ))
        , commit_( new dash::Commit )
{
}

Context::~Context()
{
    LBASSERTINFO( getCommit()->empty(), "Destroyed context has active changes");
    delete commit_;
    commit_ = 0;

    freeSlots_.push_back( slot_ );
    if( slot_ == 0 ) // main context dtor
    {
        LBASSERTINFO( int32_t( freeSlots_.size( )) == numSlots_,
                      "Active context during main context destruction? : " <<
                      freeSlots_.size() << " != " << numSlots_ );
        freeSlots_.clear();
        numSlots_ = 0;
    }
}

Context& Context::getCurrent()
{
    return dash::Context::getCurrent().getImpl();
}

size_t Context::getNumSlots()
{
    return numSlots_;
}

void Context::map( dash::NodePtr node, const Context& to )
{
    if( !getCommit()->empty( ))
    {
        std::stringstream out;
        out << "Source dash::context.has pending changes: " << getCommit()
            << ", called from " << lunchbox::backtrace;
        throw std::runtime_error( out.str( ));
    }

    MapVisitor mapper( *this, to );
    node->accept( mapper );
}

void Context::unmap( dash::NodePtr node )
{
    UnmapVisitor unmapper( *this );
    node->accept( unmapper );
}

void Context::map( AttributePtr attribute, const Context& to )
{
    attribute->map( *this, to );
}

void Context::unmap( AttributePtr attribute )
{
    attribute->unmap( *this );
}

CommitPtr Context::getCommit()
{
    return commit_->getImpl();
}

CommitConstPtr Context::getCommit() const
{
    return commit_->getImpl();
}

void Context::addChange( const Change& change )
{
    if( numSlots_ > 1 ) // OPT: Single context does not need to record changes
        getCommit()->add( change );
}

dash::Commit Context::commit()
{
    LB_TS_SCOPED( thread_ );

    dash::Commit current = *commit_;
    delete commit_;
    commit_ = new dash::Commit;
    return current;
}

void Context::apply( CommitConstPtr cmt )
{
    LB_TS_SCOPED( thread_ );
    cmt->apply();
}

}
}