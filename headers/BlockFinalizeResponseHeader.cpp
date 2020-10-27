/*
    Copyright (C) 2019 SKALE Labs

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

    @file BlockFinalizeResponseHeader.cpp
    @author Stan Kladko
    @date 2019
*/


#include "SkaleCommon.h"
#include "Log.h"
#include "exceptions/InvalidArgumentException.h"
#include "thirdparty/json.hpp"

#include "AbstractBlockRequestHeader.h"
#include "BlockFinalizeResponseHeader.h"

BlockFinalizeResponseHeader::BlockFinalizeResponseHeader() : Header(Header::BLOCK_FINALIZE_RSP ) {

}


void BlockFinalizeResponseHeader::addFields(rapidjson::Writer<rapidjson::StringBuffer> &_j) {


    CHECK_STATE(isComplete())
    Header::addFields(_j);

    if (getStatusSubStatus().first != CONNECTION_PROCEED)
        return;

    CHECK_STATE(!blockHash.empty())

    _j.String("blockHash");
    _j.String(blockHash.c_str());

    _j.String("fragmentSize");
    _j.Uint64(fragmentSize);

    _j.String("blockSize");
    _j.Uint64(blockSize);
}

void BlockFinalizeResponseHeader::setFragmentParams(uint64_t _fragmentSize, uint64_t _blockSize, const string& _hash) {

    CHECK_ARGUMENT(_fragmentSize > 2)
    CHECK_ARGUMENT(_blockSize > 16)
    CHECK_ARGUMENT(!_hash.empty())


    fragmentSize = _fragmentSize;
    blockSize = _blockSize;
    blockHash = _hash;
    setComplete();
}
