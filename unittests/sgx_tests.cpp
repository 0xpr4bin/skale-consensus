//
// Created by kladko on 17.06.20.
//

TEST_CASE_METHOD(StartFromScratch, "Test sgx server connection", "[sgx]") {

string certDir("/tmp");

CryptoManager::generateSSLClientCertAndKey(certDir);

auto certFilePath = certDir + "/cert";
auto keyFilePath = certDir + "/key";

CryptoManager::setSGXKeyAndCert(keyFilePath, certFilePath);

setenv("sgxKeyFileFullPath", keyFilePath.data(), 1);
setenv("certFileFullPath", certFilePath.data(), 1);

jsonrpc::HttpClient client("https://localhost:" + to_string(SGX_SSL_PORT));
auto c  = make_shared<StubClient>(client, jsonrpc::JSONRPC_CLIENT_V2);

vector<ptr<string>> keyNames;
vector<ptr<string>> publicKeys;

using namespace CryptoPP;

for (int i = 1; i <= 4; i++) {
auto res = CryptoManager::generateSGXECDSAKey(c);
auto keyName = res.first;
auto publicKey = res.second;

setenv(("sgxECDSAKeyName." + to_string(i)).data(), keyName->data(), 1);
setenv(("sgxECDSAPublicKey." + to_string(i)).data(), publicKey->data(), 1);

keyNames.push_back(keyName);
publicKeys.push_back(publicKey);
}




CryptoManager cm( 4, 3, make_shared<string>("127.0.0.1"),
                  make_shared<string>(keyFilePath),
                  make_shared<string>("certFilePath"),
                  keyNames.at(0), publicKeys);

auto msg = make_shared<vector<uint8_t>>();
msg->push_back('1');
auto hash = SHAHash::calculateHash(msg);
auto sig = cm.sgxSignECDSA( hash, *keyNames[0] );

REQUIRE(cm.sgxVerifyECDSA(hash, publicKeys[0], sig));

auto key = CryptoManager::decodeSGXPublicKey(publicKeys[0]);

SUCCEED();

}


TEST_CASE("Parse sgx keys", "[sgx-parse]") {
    ptr<vector<string>> ecdsaKeyNames = nullptr;
    ptr<vector<string>> blsKeyNames = nullptr;
    ptr<vector<string>> ecdsaPublicKeys = nullptr;
    ptr<vector<ptr<vector<string>>>> blsPublicKeys = nullptr;
    ptr<vector<string>> blsPublicKey = nullptr;

    tie(ecdsaKeyNames, ecdsaPublicKeys, blsKeyNames, blsPublicKeys, blsPublicKey) =
        JSONFactory::parseTestKeyNamesFromJson( "run_sgx_test/sgx_data/4node.json", 4, 1 );

    tie(ecdsaKeyNames, ecdsaPublicKeys, blsKeyNames, blsPublicKeys, blsPublicKey) =
        JSONFactory::parseTestKeyNamesFromJson( "run_sgx_test/sgx_data/16node.json", 16, 5 );



}