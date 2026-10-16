#include <gtest/gtest.h>
#include <helpers/OutPath.h>
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>
// Only storage and key derivation are faked. ClientACL's real record format runs below.
class File {
 std::shared_ptr<std::vector<uint8_t>> data;size_t offset=0;
public:
 File(std::shared_ptr<std::vector<uint8_t>> d={}):data(d){}
 explicit operator bool()const{return bool(data);}
 size_t read(uint8_t* out,size_t n) {n=std::min(n,data->size()-offset);memcpy(out,data->data()+offset,n);offset+=n;return n;}
 size_t write(const uint8_t* in,size_t n) {data->insert(data->end(),in,in+n);return n;}
 void close(){}
};
struct FakeFS {
 std::map<std::string,std::shared_ptr<std::vector<uint8_t>>> files;
 void mkdir(const char*){}
 bool exists(const char* key){return files.count(key)>0;}
 void remove(const char* key){files.erase(key);}
 File open(const char* key,const char* mode="r",bool=false) {
  if (*mode=='w')files[key]=std::make_shared<std::vector<uint8_t>>();
  return File(files[key]);
 }
};
#define FILESYSTEM FakeFS
#include "../../src/helpers/ClientACL.cpp"
namespace mesh {
Identity::Identity(){memset(pub_key,0,sizeof(pub_key));}
LocalIdentity::LocalIdentity():Identity(){}
void LocalIdentity::calcSharedSecret(uint8_t* out,const uint8_t*)const{memset(out,0,PUB_KEY_SIZE);}
}
static bool parse(const char* text,uint8_t* path,uint8_t& len) {
 std::vector<char> input(text,text+strlen(text)+1);const char* err=nullptr;
 return parsePathCommand(input.data(),path,len,err);
}
TEST(OutPath, ManualHashWidthsAndModes) {
 uint8_t path[MAX_PATH_SIZE]={},len=0;char reply[160];
 for (const auto& spec: {std::string("aa,bb"),std::string("aabb,ccdd"),std::string("aabbcc,ddeeff")}) {
  ASSERT_TRUE(parse(spec.c_str(),path,len));EXPECT_TRUE(mesh::Packet::isValidPathLen(len));EXPECT_EQ(2,len&63);
 }
 ASSERT_TRUE(parse(" flood ",path,len));EXPECT_EQ(OUT_PATH_FORCE_FLOOD,len);
 formatPathReply(path,len,reply,sizeof(reply));EXPECT_STREQ("> flood",reply);
 ASSERT_TRUE(parse("clear",path,len));EXPECT_EQ(OUT_PATH_UNKNOWN,len);
 ASSERT_TRUE(parse("direct",path,len));EXPECT_EQ(0,len);
}
TEST(OutPath, InvalidPathDoesNotModifyExistingRoute) {
 uint8_t path[MAX_PATH_SIZE],len=7;memset(path,0x55,sizeof(path));
 for (const char* spec:{"", "aa,", ",aa", "aa,,bb", "aa,bbbb", "zz", "a", "aabbccdd"}) {
  EXPECT_FALSE(parse(spec,path,len))<<spec;EXPECT_EQ(7,len);EXPECT_EQ(0x55,path[0]);
 }
 std::string many;
 for(int n=0;n<64;++n)many+=(n?",aa":"aa");
 EXPECT_FALSE(parse(many.c_str(),path,len));EXPECT_EQ(7,len);
}
TEST(OutPath, LearnedPathCannotOverrideForcedFlood) {
 uint8_t path[MAX_PATH_SIZE]={},learned[]={0xaa,0xbb};uint8_t len=OUT_PATH_FORCE_FLOOD;
 learnOutPath(path,len,learned,2);EXPECT_EQ(OUT_PATH_FORCE_FLOOD,len);
 len=OUT_PATH_UNKNOWN;learnOutPath(path,len,learned,2);EXPECT_EQ(2,len);EXPECT_EQ(0xbb,path[1]);
}
TEST(OutPath, ACLPersistencePreservesManualAndFloodRoutesAfterRestart) {
 FakeFS fs;ClientACL acl;mesh::LocalIdentity self;
 uint8_t key[PUB_KEY_SIZE]={};
 for(const char* spec:{"aa,bb", "aabbcc,ddeeff", "flood", "direct", "clear"}) {
  ++key[0];auto* client=acl.putClient(mesh::Identity(key),PERM_ACL_ADMIN);
  ASSERT_TRUE(parse(spec,client->out_path,client->out_path_len));
 }
 acl.save(&fs);
 ClientACL restored;restored.load(&fs,self);ASSERT_EQ(5,restored.getNumClients());
 for(int i=0;i<5;++i) {
  EXPECT_EQ(acl.getClientByIdx(i)->out_path_len,restored.getClientByIdx(i)->out_path_len);
  EXPECT_EQ(0,memcmp(acl.getClientByIdx(i)->out_path,restored.getClientByIdx(i)->out_path,MAX_PATH_SIZE));
 }
}
int main(int argc,char**argv) {testing::InitGoogleTest(&argc,argv);return RUN_ALL_TESTS();}
