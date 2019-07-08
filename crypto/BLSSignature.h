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

    @file BLSSignature.h
    @author Stan Kladko
    @date 2019
*/

#ifndef SKALED_BLSSIGNATURE_H
#define SKALED_BLSSIGNATURE_H

#include <stdlib.h>
#include <string>

namespace libff {
    class alt_bn128_G1;
}

class BLSSignature {
protected:
    std::shared_ptr<libff::alt_bn128_G1> sig;
public:

    BLSSignature(std::shared_ptr<std::string> s);
    BLSSignature( const std::shared_ptr< libff::alt_bn128_G1 >& sig );
    std::shared_ptr<libff::alt_bn128_G1> getSig() const;
    std::shared_ptr<std::string> toString();

    static void checkSigners( uint64_t _requiredSigners, uint64_t _totalSigners );


};



#endif //SKALED_BLSSIGNATURE_H
