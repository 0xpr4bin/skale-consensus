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

    @file MessageEnvelope.cpp
    @author Stan Kladko
    @date 2018
*/

#include "SkaleCommon.h"
#include "Log.h"
#include "exceptions/FatalError.h"

#include "protocols/ProtocolKey.h"
#include "node/NodeInfo.h"
#include "messages/Message.h"
#include "MessageEnvelope.h"



ptr<Message> MessageEnvelope::getMessage() const {
    CHECK_STATE(message);
    return message;
}


MessageOrigin MessageEnvelope::getOrigin() const {
    return origin;
}

MessageEnvelope::MessageEnvelope(MessageOrigin origin, const ptr<Message> &_message,
                                 const ptr<NodeInfo> &_realSender) : origin(origin), message(_message),
                                                                     srcNodeInfo(_realSender) {
    CHECK_ARGUMENT(_message);
    CHECK_ARGUMENT(_realSender);
}

MessageEnvelope::~MessageEnvelope() {}
