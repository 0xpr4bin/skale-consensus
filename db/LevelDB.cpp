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

    @file LevelDB.cpp
    @author Stan Kladko
    @date 2019
*/


#include "../SkaleCommon.h"
#include "../Log.h"
#include "../thirdparty/json.hpp"
#include "leveldb/db.h"

#include "../chains/Schain.h"
#include "../datastructures/TransactionList.h"
#include "../datastructures/Transaction.h"
#include "../datastructures/PartialHashesList.h"
#include "../node/Node.h"
#include "../datastructures/MyBlockProposal.h"
#include "../datastructures/BlockProposal.h"
#include "../crypto/SHAHash.h"
#include "../exceptions/LevelDBException.h"
#include "../exceptions/FatalError.h"

#include "LevelDB.h"


using namespace leveldb;


static WriteOptions writeOptions;
static ReadOptions readOptions;


ptr<string> LevelDB::readString(string &_key) {

    shared_lock<shared_mutex> lock(m);

    for (int i = LEVELDB_PIECES - 1; i >= 0; i--) {
        auto result = make_shared<string>();
        ASSERT(db[i] != nullptr);
        auto status = db[i]->Get(readOptions, _key, &*result);
        throwExceptionOnError(status);
        if (!status.IsNotFound())
            return result;
    }

    return nullptr;
}

bool LevelDB::keyExists(const char *_key) {

    shared_lock<shared_mutex> lock(m);

    for (int i = LEVELDB_PIECES - 1; i >= 0; i--) {
        auto result = make_shared<string>();
        ASSERT(db[i] != nullptr);
        auto status = db[i]->Get(readOptions, _key, &*result);
        throwExceptionOnError(status);
        if (!status.IsNotFound())
            return true;
    }

    return false;
}



void LevelDB::writeString(const string &_key, const string &_value) {

    rotateDBsIfNeeded();

    {
        shared_lock<shared_mutex> lock(m);

        auto status = db.back()->Put(writeOptions, Slice(_key), Slice(_value));

        throwExceptionOnError(status);
    }
}

uint64_t LevelDB::getActiveDBSize() {
    static leveldb::Range all("", "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
    uint64_t size;
    db.back()->GetApproximateSizes(&all, 1, &size);

    return size;
}

void LevelDB::writeByteArray(const char *_key, size_t _keyLen, const char *value,
                             size_t _valueLen) {

    rotateDBsIfNeeded();

    {
        shared_lock<shared_mutex> lock(m);

        auto status = db.back()->Put(writeOptions, Slice(_key, _keyLen), Slice(value, _valueLen));

        throwExceptionOnError(status);
    }
}

void LevelDB::writeByteArray(string &_key, const char *value,
                             size_t _valueLen) {

    rotateDBsIfNeeded();

    {
        shared_lock<shared_mutex> lock(m);

        getActiveDBSize();

        auto status = db.back()->Put(writeOptions, Slice(_key), Slice(value, _valueLen));

        throwExceptionOnError(status);

    }
}

void LevelDB::throwExceptionOnError(Status _status) {
    if (_status.IsNotFound())
        return;

    if (!_status.ok()) {
        BOOST_THROW_EXCEPTION(LevelDBException("Could not write to database:" +
                                               _status.ToString(), __CLASS_NAME__));
    }

}

uint64_t LevelDB::visitKeys(LevelDB::KeyVisitor *_visitor, uint64_t _maxKeysToVisit) {

    shared_lock<shared_mutex> lock(m);

    uint64_t readCounter = 0;

    leveldb::Iterator *it = db.back()->NewIterator(readOptions);
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        _visitor->visitDBKey(it->key().data());
        readCounter++;
        if (readCounter >= _maxKeysToVisit) {
            break;
        }
    }

    delete it;

    return readCounter;
}


DB * LevelDB::openDB(uint64_t _index) {

    leveldb::DB *dbase = nullptr;

    static leveldb::Options options;
    options.create_if_missing = true;

    ASSERT2(leveldb::DB::Open(options, dirname + "/" + prefix + "." + to_string(_index),
                              &dbase).ok(),
            "Unable to open database");
    return dbase;
}


LevelDB::LevelDB(string &_dirName, string &_prefix, node_id _nodeId,
                 uint64_t _maxDBSize) : nodeId(_nodeId),
                                        prefix(_prefix), dirname(_dirName),
                                        maxDBSize(_maxDBSize) {

    boost::filesystem::path path(_dirName);

    CHECK_ARGUMENT(_maxDBSize != 0);

    highestDBIndex = findMaxMinDBIndex(make_shared<string>(
            _prefix + "."), path).first;

    if (highestDBIndex < LEVELDB_PIECES) {
        highestDBIndex = LEVELDB_PIECES;
    }


    for (auto i = highestDBIndex - LEVELDB_PIECES + 1; i <= highestDBIndex; i++) {
        leveldb::DB *dbase = openDB(i);
        db.push_back(shared_ptr<leveldb::DB>(dbase));
    }
}

LevelDB::~LevelDB() {
}

using namespace boost::filesystem;

std::pair<uint64_t, uint64_t> LevelDB::findMaxMinDBIndex(ptr<string> _prefix, boost::filesystem::path _path) {

    CHECK_ARGUMENT(_prefix != nullptr);

    vector<path> dirs;
    vector<uint64_t> indices;

    copy(directory_iterator(_path), directory_iterator(), back_inserter(dirs));
    sort(dirs.begin(), dirs.end());

    for (auto &path : dirs) {
        if (is_directory(path)) {
            auto fileName = path.filename().string();
            if (fileName.find(*_prefix) == 0) {
                auto index = fileName.substr(_prefix->size());
                auto value = strtoull(index.c_str(), nullptr, 10);
                if (value != 0) {
                    indices.push_back(value);
                }
            }
        }
    }

    if (indices.size() == 0)
        return {0, 0};

    auto maxIndex = *max_element(begin(indices), end(indices));
    auto minIndex = *min_element(begin(indices), end(indices));

    return {maxIndex, minIndex};
}

void LevelDB::rotateDBsIfNeeded() {

    if (getActiveDBSize() <= maxDBSize)
        return;

    {
        lock_guard<shared_mutex> lock(m);

        auto newDB = openDB(highestDBIndex+1);

        for (int i = 1; i <  LEVELDB_PIECES; i++) {
            db.at(i -1) = db.at(i);
        }

        db[LEVELDB_PIECES - 1] = shared_ptr<leveldb::DB>(newDB);

        db.at(0) = nullptr;
        highestDBIndex++;
    }
}

