//
// Created by kladko on 7/4/19.
//


#include <stdlib.h>
#include <string>
#include "bls_include.h"

using namespace std;

#include "BLSSigShare.h"


shared_ptr< libff::alt_bn128_G1 > BLSSigShare::getSigShare() const {
    return sigShare;
}
size_t BLSSigShare::getSignerIndex() const {
    return signerIndex;
}

shared_ptr< string > BLSSigShare::toString() {
    char str[512];

    gmp_sprintf( str, "%Nd:%Nd", sigShare->X.as_bigint().data, libff::alt_bn128_Fq::num_limbs,
        sigShare->Y.as_bigint().data, libff::alt_bn128_Fq::num_limbs );

    return make_shared< string >( str );
}

BLSSigShare::BLSSigShare( shared_ptr< string > _sigShare, size_t signerIndex )
    : signerIndex( signerIndex ) {
    if ( signerIndex == 0 ) {
        BOOST_THROW_EXCEPTION( runtime_error( "Zero signer index" ) );
    }

    if ( !_sigShare ) {
        BOOST_THROW_EXCEPTION( runtime_error( "Null _sigShare" ) );
    }


    if ( _sigShare->size() < 10 ) {
        BOOST_THROW_EXCEPTION(
            runtime_error( "Signature too short:" + to_string( _sigShare->size() ) ) );
    }

    if ( _sigShare->size() > BLS_MAX_SIG_LEN ) {
        BOOST_THROW_EXCEPTION(
            runtime_error( "Signature too long:" + to_string( _sigShare->size() ) ) );
    }

    auto position = _sigShare->find( ":" );

    if ( position == string::npos ) {
        BOOST_THROW_EXCEPTION( runtime_error( "Misformatted sig:" + *_sigShare ) );
    }

    if ( position >= BLS_MAX_COMPONENT_LEN ||
         _sigShare->size() - position > BLS_MAX_COMPONENT_LEN ) {
        BOOST_THROW_EXCEPTION( runtime_error( "Misformatted sig:" + *_sigShare ) );
    }


    auto component1 = _sigShare->substr( 0, position );
    auto component2 = _sigShare->substr( position + 1 );


    for ( char& c : component1 ) {
        if ( !( c >= '0' && c <= '9' ) ) {
            BOOST_THROW_EXCEPTION( runtime_error(
                "Misformatted char:" + to_string( ( int ) c ) + " in component 1:" + component1 ) );
        }
    }


    for ( char& c : component2 ) {
        if ( !( c >= '0' && c <= '9' ) ) {
            BOOST_THROW_EXCEPTION( runtime_error(
                "Misformatted char:" + to_string( ( int ) c ) + " in component 2:" + component2 ) );
        }
    }


    libff::bigint< 4 > X( component1.c_str() );
    libff::bigint< 4 > Y( component2.c_str() );
    libff::bigint< 4 > Z( "1" );

    sigShare = make_shared< libff::alt_bn128_G1 >( X, Y, Z );
}
BLSSigShare::BLSSigShare( const shared_ptr< libff::alt_bn128_G1 >& _sigShare, size_t _signerIndex )
    : sigShare( _sigShare ), signerIndex( _signerIndex ) {
    if ( _signerIndex == 0 ) {
        BOOST_THROW_EXCEPTION( runtime_error( "Zero signer index" ) );
    }

    if ( !_sigShare ) {
        BOOST_THROW_EXCEPTION( runtime_error( "Null _s" ) );
    }
}
