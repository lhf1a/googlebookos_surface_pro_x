#pragma once
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <vector>

// Annex-B CSD only. Keep header-only work out of the hardware frame queue.
// Configuration work can be returned once copied; consume it only after a
// following access unit was accepted. Not an avcC/length-prefix converter.
class Sq2AvcConfig {
 std::vector<uint8_t> bytes_;
public:
 static constexpr size_t Limit=1024*1024;
 void clear(){bytes_.clear();}
 size_t size()const{return bytes_.size();}
 int append(const uint8_t* data,size_t size){
  if(!data||!size)return -EINVAL;
  if(size>Limit-bytes_.size())return -EMSGSIZE;
  bytes_.insert(bytes_.end(),data,data+size);return 0;
 }
 template<class Submit> int submit(const uint8_t* data,size_t size,Submit send){
  if(!data||!size)return -EINVAL;
  if(size>Limit-bytes_.size())return -EMSGSIZE;
  if(bytes_.empty())return send(data,size);
  std::vector<uint8_t> joined(bytes_);
  joined.insert(joined.end(),data,data+size);
  int result=send(joined.data(),joined.size());
  if(!result)clear();
  return result;
 }
};
