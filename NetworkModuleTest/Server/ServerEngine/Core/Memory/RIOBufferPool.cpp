#ifdef _WIN32
#include "RIOBufferPool.h"

#include <numeric>

namespace Core {
namespace Memory {

RIOBufferPool::~RIOBufferPool()
{
    Shutdown();
}

bool RIOBufferPool::Initialize(size_t poolSize, size_t slotSize)
{
    if (poolSize == 0 || slotSize == 0)
        return false;

    std::lock_guard<std::mutex> lock(mMutex);
    if (mSlab)
        return false; // already initialized

    // Load RIO register/deregister function pointers via a temporary socket.
    SOCKET tempSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                  nullptr, 0, WSA_FLAG_REGISTERED_IO);
    if (tempSocket == INVALID_SOCKET)
        return false;

    GUID functionTableId = WSAID_MULTIPLE_RIO;
    RIO_EXTENSION_FUNCTION_TABLE rioFunctionTable;
    DWORD bytes = 0;

    int result = WSAIoctl(tempSocket,
                          SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
                          &functionTableId, sizeof(functionTableId),
                          &rioFunctionTable, sizeof(rioFunctionTable),
                          &bytes, nullptr, nullptr);
    closesocket(tempSocket);

    if (result == SOCKET_ERROR)
        return false;

    mPfnRegisterBuffer   = rioFunctionTable.RIORegisterBuffer;
    mPfnDeregisterBuffer = rioFunctionTable.RIODeregisterBuffer;

    if (!mPfnRegisterBuffer || !mPfnDeregisterBuffer)
        return false;

    // Allocate the slab.
    const SIZE_T slabBytes = poolSize * slotSize;
    mSlab = VirtualAlloc(nullptr, slabBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mSlab)
        return false;

    // Register once (single RIO_BUFFERID for the entire slab).
    mSlabId = mPfnRegisterBuffer(
        reinterpret_cast<PCHAR>(mSlab), static_cast<DWORD>(slabBytes));
    if (mSlabId == RIO_INVALID_BUFFERID) {
        VirtualFree(mSlab, 0, MEM_RELEASE);
        mSlab = nullptr;
        return false;
    }

    mSlotSize = slotSize;
    mPoolSize = poolSize;

    mFreeIndices.resize(poolSize);
    std::iota(mFreeIndices.begin(), mFreeIndices.end(), size_t(0));

    return true;
}

void RIOBufferPool::Shutdown()
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (mSlabId != RIO_INVALID_BUFFERID && mPfnDeregisterBuffer) {
        mPfnDeregisterBuffer(mSlabId);
        mSlabId = RIO_INVALID_BUFFERID;
    }
    if (mSlab) {
        VirtualFree(mSlab, 0, MEM_RELEASE);
        mSlab = nullptr;
    }
    mFreeIndices.clear();
    mSlotSize = 0;
    mPoolSize = 0;
}

BufferSlot RIOBufferPool::Acquire()
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mFreeIndices.empty())
        return {};

    const size_t idx = mFreeIndices.back();
    mFreeIndices.pop_back();
    void* ptr = reinterpret_cast<char*>(mSlab) + idx * mSlotSize;
    return BufferSlot{ptr, idx, mSlotSize};
}

void RIOBufferPool::Release(size_t index)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mFreeIndices.push_back(index);
}

size_t RIOBufferPool::FreeCount() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mFreeIndices.size();
}

uint64_t RIOBufferPool::GetRIOBufferId(size_t /*index*/) const
{
    // RIO_BUFFERID is PVOID on Windows; cast to uint64_t for the base interface.
    return reinterpret_cast<uint64_t>(mSlabId);
}

char* RIOBufferPool::SlotPtr(size_t index) const
{
    // Called from ProcessCompletions (no lock needed: mSlab is immutable post-Initialize).
    return reinterpret_cast<char*>(mSlab) + index * mSlotSize;
}

} // namespace Memory
} // namespace Core

#endif // _WIN32
