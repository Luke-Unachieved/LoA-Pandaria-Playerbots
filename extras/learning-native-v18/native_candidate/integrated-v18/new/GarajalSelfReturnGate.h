#pragma once
#include <cstdint>
namespace MopRaidActions {
inline bool SameReturnCapability(std::uint64_t supplied,std::uint64_t nativeToken,std::uint64_t currentEpoch,std::uint64_t entryEpoch,bool sameCallback,bool alreadyIssued){
 return supplied&&supplied==nativeToken&&currentEpoch&&currentEpoch==entryEpoch&&sameCallback&&!alreadyIssued;
}
}
