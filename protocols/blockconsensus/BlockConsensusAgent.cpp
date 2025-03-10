/*
    Copyright (C) 2018-2019 SKALE Labs

    This file is part of skale-consensus.

    skale-consensus is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    skale-consensus is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with skale-consensus.  If not, see <https://www.gnu.org/licenses/>.

    @file BlockConsensusAgent.cpp
    @author Stan Kladko
    @date 2018-
*/

#include "SkaleCommon.h"
#include "Log.h"
#include "exceptions/FatalError.h"
#include "thirdparty/json.hpp"

#include "blockfinalize/client/BlockFinalizeDownloader.h"
#include "blockfinalize/client/BlockFinalizeDownloaderThreadPool.h"
#include "blockproposal/pusher/BlockProposalClientAgent.h"
#include "chains/Schain.h"
#include "crypto/BLAKE3Hash.h"
#include "crypto/ThresholdSigShare.h"
#include "crypto/CryptoManager.h"
#include "datastructures/BlockProposal.h"
#include "datastructures/BooleanProposalVector.h"
#include "datastructures/TransactionList.h"
#include "db/BlockDB.h"
#include "db/BlockProposalDB.h"
#include "db/BlockSigShareDB.h"
#include "exceptions/ExitRequestedException.h"
#include "exceptions/InvalidStateException.h"
#include "messages/ConsensusProposalMessage.h"
#include "messages/InternalMessageEnvelope.h"
#include "messages/NetworkMessage.h"
#include "messages/NetworkMessageEnvelope.h"
#include "messages/ParentMessage.h"
#include "monitoring/OptimizerAgent.h"
#include "network/Network.h"
#include "node/Node.h"
#include "node/NodeInfo.h"
#include "pendingqueue/PendingTransactionsAgent.h"
#include "protocols/blockconsensus/BlockSignBroadcastMessage.h"
#include "thirdparty/lrucache.hpp"

#include "utils/Time.h"
#include "protocols/ProtocolKey.h"
#include "protocols/binconsensus/BVBroadcastMessage.h"
#include "protocols/binconsensus/BinConsensusInstance.h"

#include "crypto/ThresholdSignature.h"
#include "protocols/binconsensus/ChildBVDecidedMessage.h"
#include "BlockConsensusAgent.h"
#include "datastructures/CommittedBlock.h"


BlockConsensusAgent::BlockConsensusAgent( Schain& _schain )
    : ProtocolInstance( BLOCK_SIGN, _schain ) {
    trueDecisions = make_shared<
        cache::lru_cache< uint64_t, ptr< map< schain_index, ptr< ChildBVDecidedMessage > > > > >(
        MAX_CONSENSUS_HISTORY );
    falseDecisions = make_shared<
        cache::lru_cache< uint64_t, ptr< map< schain_index, ptr< ChildBVDecidedMessage > > > > >(
        MAX_CONSENSUS_HISTORY );
    decidedIndices =
        make_shared< cache::lru_cache< uint64_t, schain_index > >( MAX_CONSENSUS_HISTORY );


    BinConsensusInstance::initHistory( _schain.getNodeCount() );


    for ( int i = 0; i < _schain.getNodeCount(); i++ ) {
        children.push_back(
            make_shared< cache::lru_cache< uint64_t, ptr< BinConsensusInstance > > >(
                MAX_CONSENSUS_HISTORY ) );
    }

    auto blockDB = _schain.getNode()->getBlockDB();

    auto currentBlock = blockDB->readLastCommittedBlockID() + 1;

    for ( int i = 0; i < _schain.getNodeCount(); i++ ) {
        children[i]->put( ( uint64_t ) currentBlock,
            make_shared< BinConsensusInstance >( this, currentBlock, i + 1, true ) );
    }
};


void BlockConsensusAgent::startConsensusProposal(
    block_id _blockID, const ptr< BooleanProposalVector >& _proposal ) {
    try {
        if ( getSchain()->getLastCommittedBlockID() >= _blockID ) {
            LOG( debug, "Terminating consensus proposal since already committed." );
        }

        LOG( debug, "CONSENSUS START:BLOCK:" << to_string( _blockID ) );

        if (getSchain()->getOptimizerAgent()->doOptimizedConsensus(_blockID,
                 getSchain()->getLastCommittedBlockTimeStamp().getS())) {
            //  Optimized consensus. Start N binary consensuses
            // for optimized block consensus, we only propose and initiated binary consensus
            // for the last block winner
            auto lastWinner = getSchain()->getOptimizerAgent()->getPreviousWinner( _blockID );
            auto x =
                bin_consensus_value(_proposal->getProposalValue(schain_index(lastWinner)) ? 1 : 0);
            propose(x, lastWinner, _blockID);
            return;
        }
        // normal consensus. Start N binary consensuses
        for ( uint64_t i = 1; i <= ( uint64_t ) getSchain()->getNodeCount(); i++ ) {
            if ( getSchain()->getNode()->isExitRequested() )
                return;

            bin_consensus_value x;

            x = bin_consensus_value( _proposal->getProposalValue( schain_index( i ) ) ? 1 : 0 );

            propose( x, schain_index( i ), _blockID );
        }

    } catch ( ExitRequestedException& ) {
        throw;
    } catch ( SkaleException& e ) {
        throw_with_nested( InvalidStateException( __FUNCTION__, __CLASS_NAME__ ) );
    }
}


void BlockConsensusAgent::processChildMessageImpl( const ptr< InternalMessageEnvelope >& _me ) {
    CHECK_ARGUMENT( _me );

    auto msg = dynamic_pointer_cast< ChildBVDecidedMessage >( _me->getMessage() );

    reportConsensusAndDecideIfNeeded( msg );
}

void BlockConsensusAgent::propose(
    bin_consensus_value _proposal, schain_index _index, block_id _id ) {
    try {
        CHECK_ARGUMENT( ( uint64_t ) _index > 0 );
        auto key = make_shared< ProtocolKey >( _id, _index );

        auto child = getChild( key );

        auto msg = make_shared< BVBroadcastMessage >(
            _id, _index, bin_consensus_round( 0 ), _proposal, Time::getCurrentTimeMs(), *child );


        auto id = ( uint64_t ) msg->getBlockId();

        CHECK_STATE( id != 0 );

        child->processMessage(
            make_shared< InternalMessageEnvelope >( ORIGIN_PARENT, msg, *getSchain() ) );

    } catch ( ExitRequestedException& ) {
        throw;
    } catch ( ... ) {
        throw_with_nested( InvalidStateException( __FUNCTION__, __CLASS_NAME__ ) );
    }
}


void BlockConsensusAgent::decideBlock(
    block_id _blockId, schain_index _sChainIndex, const string& _stats ) {
    CHECK_ARGUMENT( !_stats.empty() );

    try {
        BinConsensusInstance::logGlobalStats();

        LOG( info, string( "BLOCK_DECIDED:PROPOSER:" )
                       << to_string( _sChainIndex ) << ":BID:" + to_string( _blockId ) << ":STATS:|"
                       << _stats << "| Now signing block ..." );


        auto msg = make_shared< BlockSignBroadcastMessage >(
            _blockId, _sChainIndex, Time::getCurrentTimeMs(), *this );

        auto signature = getSchain()->getNode()->getBlockSigShareDB()->checkAndSaveShareInMemory(
            msg->getSigShare(), getSchain()->getCryptoManager(), _sChainIndex );

        getSchain()->getNode()->getNetwork()->broadcastMessage( msg );

        decidedIndices->put( ( uint64_t ) _blockId, _sChainIndex );

        if ( signature != nullptr ) {
            getSchain()->finalizeDecidedAndSignedBlock( _blockId, _sChainIndex, signature );
        }

    } catch ( ExitRequestedException& ) {
        throw;
    } catch ( SkaleException& e ) {
        throw_with_nested( InvalidStateException( __FUNCTION__, __CLASS_NAME__ ) );
    }
}


void BlockConsensusAgent::decideDefaultBlock( block_id _blockNumber ) {
    try {
        decideBlock( _blockNumber, schain_index( 0 ), string( "DEFAULT_BLOCK" ) );
    } catch ( ExitRequestedException& ) {
        throw;
    } catch ( SkaleException& e ) {
        throw_with_nested( InvalidStateException( __FUNCTION__, __CLASS_NAME__ ) );
    }
}


void BlockConsensusAgent::reportConsensusAndDecideIfNeeded(
    const ptr< ChildBVDecidedMessage >& _msg ) {
    CHECK_ARGUMENT( _msg );

    try {
        auto nodeCount = ( uint64_t ) getSchain()->getNodeCount();
        schain_index blockProposerIndex = _msg->getBlockProposerIndex();
        auto blockID = _msg->getBlockId();

        if ( blockID <= getSchain()->getLastCommittedBlockID() ) {
            // Old consensus is reporting, already got this block through catchup
            return;
        }

        CHECK_STATE( blockProposerIndex <= nodeCount );

        if ( decidedIndices->exists( ( uint64_t ) blockID ) ) {
            return;
        }

        // if we are doing optimized consensus
        // we are only running a single binary consensus for the last
        // winner and ignoring all other messages, even if someone sends them by mistake
        if (getSchain()->getOptimizerAgent()->doOptimizedConsensus(blockID,
                                                                   getSchain()->getLastCommittedBlockTimeStamp().getS()) &&
            (uint64_t) blockProposerIndex !=
                 getSchain()->getOptimizerAgent()->getPreviousWinner( blockID )) {
            LOG(warn, "Consensus got ChildBVBroadcastMessage for non-winner in optimized round:" + blockProposerIndex);
            return;
        }

        // record that the binary consensus completion reported by the msg
        recordBinaryDecision( _msg, blockProposerIndex, blockID );

        if (getSchain()->getOptimizerAgent()->doOptimizedConsensus( blockID, getSchain()->getLastCommittedBlockTimeStamp().getS()) ) {
            decideOptimizedBlockConsensusIfCan( blockID );
        } else {
            decideNormalBlockConsensusIfCan( blockID );
        }


    } catch ( ExitRequestedException& ) {
        throw;
    } catch ( SkaleException& e ) {
        throw_with_nested( InvalidStateException( __FUNCTION__, __CLASS_NAME__ ) );
    }
}


void BlockConsensusAgent::processBlockSignMessage(
    const ptr< BlockSignBroadcastMessage >& _message ) {
    try {
        auto signature = getSchain()->getNode()->getBlockSigShareDB()->checkAndSaveShareInMemory(
            _message->getSigShare(), getSchain()->getCryptoManager(),
            _message->getBlockProposerIndex() );
        if ( signature == nullptr ) {
            return;
        }


        auto proposer = _message->getBlockProposerIndex();
        auto blockId = _message->getBlockId();

        LOG( info, string( "BLOCK_DECIDED_AND_SIGNED:PRPSR:" )
                       << to_string( proposer ) << ":BID:" << to_string( blockId )
                       << ":SIG:" << signature->toString() );


        getSchain()->finalizeDecidedAndSignedBlock( blockId, proposer, signature );


    } catch ( ExitRequestedException& e ) {
        throw;
    } catch ( ... ) {
        throw_with_nested( InvalidStateException( __FUNCTION__, __CLASS_NAME__ ) );
    }
};


void BlockConsensusAgent::routeAndProcessMessage( const ptr< MessageEnvelope >& _me ) {
    CHECK_ARGUMENT( _me );


    try {
        CHECK_ARGUMENT( _me->getMessage()->getBlockId() > 0 );
        CHECK_ARGUMENT( _me->getOrigin() != ORIGIN_PARENT );

        auto blockID = _me->getMessage()->getBlockId();

        // Future blockid messages shall never get to this point
        CHECK_ARGUMENT( blockID <= getSchain()->getLastCommittedBlockID() + 1 );

        if ( blockID + MAX_ACTIVE_CONSENSUSES < getSchain()->getLastCommittedBlockID() )
            return;  // message has a very old block id, ignore. They need to catchup

        if ( _me->getMessage()->getMsgType() == MSG_CONSENSUS_PROPOSAL ) {
            auto consensusProposalMessage =
                dynamic_pointer_cast< ConsensusProposalMessage >( _me->getMessage() );

            this->startConsensusProposal(
                _me->getMessage()->getBlockId(), consensusProposalMessage->getProposals() );
            return;
        }

        if ( _me->getMessage()->getMsgType() == MSG_BLOCK_SIGN_BROADCAST ) {
            auto blockSignBroadcastMessage =
                dynamic_pointer_cast< BlockSignBroadcastMessage >( _me->getMessage() );


            CHECK_STATE( blockSignBroadcastMessage );

            this->processBlockSignMessage(
                dynamic_pointer_cast< BlockSignBroadcastMessage >( _me->getMessage() ) );
            return;
        }

        if ( _me->getOrigin() == ORIGIN_CHILD ) {
            LOG( debug, "Got child message "
                            << to_string( _me->getMessage()->getBlockId() ) << ":"
                            << to_string( _me->getMessage()->getBlockProposerIndex() ) );

            auto internalMessageEnvelope = dynamic_pointer_cast< InternalMessageEnvelope >( _me );

            CHECK_STATE( internalMessageEnvelope );

            return processChildMessageImpl( internalMessageEnvelope );
        }


        ptr< ProtocolKey > key = _me->getMessage()->createProtocolKey();

        CHECK_STATE( key );

        {
            {
                auto child = getChild( key );

                if ( child != nullptr ) {
                    return child->processMessage( _me );
                }
            }
        }

    } catch ( ExitRequestedException& ) {
        throw;
    } catch ( ... ) {
        throw_with_nested( InvalidStateException( __FUNCTION__, __CLASS_NAME__ ) );
    }
}


bin_consensus_round BlockConsensusAgent::getRound( const ptr< ProtocolKey >& _key ) {
    return getChild( _key )->getCurrentRound();
}

bool BlockConsensusAgent::decided( const ptr< ProtocolKey >& _key ) {
    return getChild( _key )->decided();
}

ptr< BinConsensusInstance > BlockConsensusAgent::getChild( const ptr< ProtocolKey >& _key ) {
    CHECK_ARGUMENT( _key );

    auto bpi = _key->getBlockProposerIndex();
    auto bid = _key->getBlockID();

    CHECK_ARGUMENT( ( uint64_t ) bpi > 0 );
    CHECK_ARGUMENT( ( uint64_t ) bpi <= ( uint64_t ) getSchain()->getNodeCount() )

    try {
        LOCK( m )
        if ( !children.at( ( uint64_t ) bpi - 1 )->exists( ( uint64_t ) bid ) ) {
            children.at( ( uint64_t ) bpi - 1 )
                ->putIfDoesNotExist(
                    ( uint64_t ) bid, make_shared< BinConsensusInstance >( this, bid, bpi ) );
        }

        return children.at( ( uint64_t ) bpi - 1 )->get( ( uint64_t ) bid );

    } catch ( ExitRequestedException& ) {
        throw;
    } catch ( ... ) {
        throw_with_nested( InvalidStateException( __FUNCTION__, __CLASS_NAME__ ) );
    }
}


bool BlockConsensusAgent::shouldPost( const ptr< NetworkMessage >& _msg ) {
    if ( _msg->getMsgType() == MSG_BLOCK_SIGN_BROADCAST ) {
        return true;
    }

    auto key = _msg->createProtocolKey();
    auto currentRound = getRound( key );
    auto r = _msg->getRound();

    if ( r > currentRound + 1 ) {  // way in the future
        return false;
    }

    if ( r == currentRound + 1 ) {  // if the previous round is decided, accept messages from the
                                    // next round
        return decided( key );
    }


    return true;
}

string BlockConsensusAgent::buildStats( block_id _blockID ) {
    ptr< map< schain_index, ptr< ChildBVDecidedMessage > > > tDecisions = nullptr;
    ptr< map< schain_index, ptr< ChildBVDecidedMessage > > > fDecisions = nullptr;


    if ( auto result = trueDecisions->getIfExists( ( uint64_t ) _blockID ); result.has_value() ) {
        tDecisions = any_cast< ptr< map< schain_index, ptr< ChildBVDecidedMessage > > > >( result );
    }

    if ( auto result = falseDecisions->getIfExists( ( uint64_t ) _blockID ); result.has_value() ) {
        fDecisions = any_cast< ptr< map< schain_index, ptr< ChildBVDecidedMessage > > > >( result );
    }

    string resultStr( "" );

    for ( int i = 1; i <= getSchain()->getNodeCount(); i++ ) {
        string stats = to_string( i ) + "|";
        ptr< ChildBVDecidedMessage > msg = nullptr;
        string decision;

        if ( tDecisions && tDecisions->count( i ) != 0 ) {
            msg = tDecisions->at( i );
            decision = "1";
        } else if ( fDecisions && fDecisions->count( i ) != 0 ) {
            decision = "0";
            msg = fDecisions->at( i );
        }

        if ( msg != nullptr ) {
            auto round = to_string( msg->getRound() );
            auto processingTime = to_string( msg->getMaxProcessingTimeMs() );
            auto latencyTime = to_string( msg->getMaxLatencyTimeMs() );
            stats = stats + "D" + decision + "R" + round + "P" + processingTime + "L" +
                    latencyTime + "|";
        } else {
            stats += "*|";
        };

        resultStr.append( stats );
    }

    return resultStr;
}

bool BlockConsensusAgent::haveTrueDecision(block_id _blockId, schain_index _proposerIndex) {
    CHECK_STATE(trueDecisions)
    auto result = trueDecisions->getIfExists(((uint64_t) _blockId));
    if (!result.has_value()) {
        return false;
    }
    auto trueMap = any_cast<ptr<map<schain_index, ptr<ChildBVDecidedMessage> > > >(result);
    return trueMap->count((uint64_t) _proposerIndex) > 0;
}

bool BlockConsensusAgent::haveFalseDecision(block_id _blockId, schain_index _proposerIndex) {
    CHECK_STATE(falseDecisions);
    auto result = falseDecisions->getIfExists(((uint64_t) _blockId));
    if (!result.has_value()) {
        return false;
    }
    auto trueMap = any_cast<ptr<map<schain_index, ptr<ChildBVDecidedMessage> > > >(result);
    return trueMap->count((uint64_t) _proposerIndex) > 0;
}


void BlockConsensusAgent::decideNormalBlockConsensusIfCan(block_id _blockId) {

    auto nodeCount = (uint64_t ) getSchain()->getNodeCount();

    uint64_t priorityLeader =
        getSchain()->getOptimizerAgent()->getPriorityLeaderForBlock( _blockId );


    // we iterate over binary decisions starting from the priority leader, to
    // see if we have a sequence like 1 or 01, or 001, or 0001 etc without gaps
    // the first 1 in the sequence is the winning proposer
    // note, priorityLeader variable is numbered from 0 to N-1
    for (uint64_t i = priorityLeader; i < priorityLeader + nodeCount; i++) {
        auto proposerIndex = schain_index(i % nodeCount) + 1;

        if (haveTrueDecision(_blockId, proposerIndex)) {
            // found first 1. decide
            decideBlock(_blockId, proposerIndex, buildStats(_blockId));
            return;
        }

        if (!haveFalseDecision(_blockId, proposerIndex)) {
            // found a gap that is not yet 1 or 0
            // cant decide on the winning proposal yet
            return;
        }
        // if we are here we found 0, so we keep iterating until we find 1 or gap
    }

    // if we got to this point without returning , it means we iterated through all nodes and did
    // not see 1 or a gap. it means that all binary consensuses completed with zeroes, and we
    // need to decide a default block. There is no winner.

    // verify sanity
    for (uint64_t j = 1; j <= nodeCount; j++) {
        CHECK_STATE(haveFalseDecision(_blockId, j));
    }

    decideDefaultBlock(_blockId);
}

void BlockConsensusAgent::decideOptimizedBlockConsensusIfCan( block_id _blockId ) {
    // for optimized consensus  there is only one proposer
    // which is the previous winner of normal consensus
    // the block is decided if there is either 1 or 0 consensus for the previous winner

    schain_index lastWinner = getSchain()->getOptimizerAgent()->getPreviousWinner( _blockId );

    CHECK_STATE(lastWinner > 0)

    if ( haveTrueDecision(_blockId, lastWinner) ) {
        // last winner consensus completed with 1
        decideBlock(_blockId, lastWinner, buildStats(_blockId));
        return;
    } else if ( haveFalseDecision( _blockId, lastWinner ) ) {
        // last winner consensus completed with 0
        // since this is the only consensus we are running in optimized round
        // we need to produce default block
        // This will happen in rare situations when the last winner crashed
        decideDefaultBlock( _blockId );
    }

    // we do not yet have 1 ofr 0 for the last winner, we can not decide yet,
    // so we just return

}


void BlockConsensusAgent::recordBinaryDecision(const ptr<ChildBVDecidedMessage> &_msg, schain_index &blockProposerIndex,
                                               const block_id &blockID) {
    if (_msg->getValue()) {
        if (!trueDecisions->exists((uint64_t) blockID))
            trueDecisions->putIfDoesNotExist((uint64_t) blockID,
                                             make_shared<map<schain_index, ptr<ChildBVDecidedMessage> > >());

        auto map = trueDecisions->get( (uint64_t) blockID );
        map->emplace( blockProposerIndex, _msg );

    } else {
        if (!falseDecisions->exists((uint64_t) blockID))
            falseDecisions->putIfDoesNotExist( (uint64_t) blockID,
                                               make_shared<map<schain_index, ptr<ChildBVDecidedMessage> > >() );

        auto map = falseDecisions->get( (uint64_t) blockID );
        map->emplace( blockProposerIndex, _msg );
    }
}