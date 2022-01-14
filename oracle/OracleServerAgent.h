/*
    Copyright (C) 2021- SKALE Labs

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

    @file OracleServerAgent.h
    @author Stan Kladko
    @date 2021-
*/

#pragma once

class Schain;

class OracleRequestBroadcastMessage;
class CryptoManager;


#include "protocols/ProtocolInstance.h"


#include "thirdparty/lrucache.hpp"
#include "sgxwallet/third_party/concurrentqueue.h"
#include "sgxwallet/third_party/readerwriterqueue.h"

using namespace moodycamel;

class MessageEnvelope;
class OracleResponseMessage;
class OracleRequestBroadcastMessage;
class OracleThreadPool;

class OracleServerAgent : public Agent {

    vector<shared_ptr<BlockingReaderWriterQueue<shared_ptr<MessageEnvelope>>>> incomingQueues;

    atomic<uint64_t> requestCounter;

    atomic<uint64_t> threadCounter;

    ptr <OracleThreadPool> oracleThreadPool = nullptr;


    ptr<OracleResponseMessage> doEndpointRequestResponse(ptr<OracleRequestBroadcastMessage> /* _request */);


    void sendOutResult(ptr<OracleResponseMessage> _msg, schain_index /* _destination*/);


public:

    OracleServerAgent(Schain& _schain);

    virtual ~OracleServerAgent() {};

    void routeAndProcessMessage(const ptr<MessageEnvelope>& _me );

    static void workerThreadItemSendLoop(OracleServerAgent* _agent );

    uint64_t curlHttpGet(const string &_uri, string& _result);

    ptr<vector<string>> extractResults(
            string& _response, vector<string> & _jsps) const;

    void trimResults(ptr<vector<string>> &_results, vector<uint64_t> &_trims) const;

    void appendResultsToSpec(string &_specStr, ptr<vector<string>> &_results) const;

    void appendErrorToSpec(string &specStr, uint64_t _error) const;
};

