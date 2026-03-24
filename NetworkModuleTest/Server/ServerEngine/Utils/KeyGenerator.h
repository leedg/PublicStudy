#pragma once

// KeyGenerator — collision-free unique key generation for server components.
// 한글: 충돌 없는 고유 키 생성 공용 모듈.
//
// Key layout (uint64_t):
//   bit 63..56  tag  (8 bits) : component type  (KeyTag enum)
//   bit 55..48  slot (8 bits) : worker / instance index (0–255)
//   bit 47.. 0  seq  (48 bits): monotonic sequence (1 = first; 0 = kInvalid)
//
// Wrap-around: seq covers 2^48 = 281,474,976,710,655 values.
//   At 1,000,000 ops/s per (tag,slot): wraps after ~8,900 years.
//   0 is permanently reserved as the "invalid / unset" sentinel.
//
// Two usage patterns:
//   A) Instance mode — embeds tag + slot in the key (for routing)
//      KeyGenerator gen(KeyTag::DBQuery, workerIndex);
//      uint64_t id = gen.Next();
//      uint8_t slot = KeyGenerator::GetSlot(id);  // recover workerIndex
//
//   B) Global mode — plain monotonic unique ID (no tag/slot structure)
//      uint64_t id = KeyGenerator::NextGlobalId();

#include <atomic>
#include <cstdint>

namespace Network::Utils
{

// =============================================================================
// KeyTag — component type embedded in bits 63..56 of every keyed id.
// 한글: 키 ID의 bit 63..56에 내장된 컴포넌트 타입 식별자.
// =============================================================================

enum class KeyTag : uint8_t
{
	None    = 0,
	DBQuery = 1,  // DBServerTaskQueue → requestId
	Session = 2,  // SessionManager   → sessionId
	WAL     = 3,  // DBTaskQueue      → walSeq
};

// =============================================================================
// KeyGenerator
// =============================================================================

class KeyGenerator
{
public:
	using KeyId = uint64_t;

	static constexpr KeyId kInvalid = 0;  // 유효하지 않은 키 sentinel (seq 비트가 모두 0)

	// Constructor — tag and slot are embedded in every key produced.
	// 한글: 생성자 — 모든 키에 tag와 slot이 내장됨.
	KeyGenerator(KeyTag tag, uint8_t slot) noexcept
		: mPrefix((static_cast<uint64_t>(tag)  << 56) |
		          (static_cast<uint64_t>(slot) << 48))
		, mSeq(0)
	{}

	// ── Static helpers ──────────────────────────────────────────────────────

	// Extract tag (bits 63..56)
	[[nodiscard]] static KeyTag GetTag(KeyId id) noexcept
	{
		return static_cast<KeyTag>(static_cast<uint8_t>(id >> 56));
	}

	// Extract slot (bits 55..48)
	[[nodiscard]] static uint8_t GetSlot(KeyId id) noexcept
	{
		return static_cast<uint8_t>((id >> 48) & 0xFFu);
	}

	// Extract sequence (bits 47..0)
	[[nodiscard]] static uint64_t GetSeq(KeyId id) noexcept
	{
		return id & kSeqMask;
	}

	[[nodiscard]] static bool IsValid(KeyId id) noexcept
	{
		return (id & kSeqMask) != 0;
	}

	// ── Pattern B: global unique ID ─────────────────────────────────────────
	// Plain monotonic uint64_t. Thread-safe, lock-free. Starts at 1.
	// 한글: 단순 단조 증가 uint64_t. 락-프리, 1부터 시작.
	[[nodiscard]] static KeyId NextGlobalId() noexcept
	{
		return sGlobalSeq.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	// ── Pattern A: instance mode (tag + slot embedded) ──────────────────────
	// Returns next id with tag+slot embedded. Thread-safe, lock-free.
	// seq wraps 0→1 (skips 0). Practical wrap: ~8,900 years at 1M/s.
	// 한글: tag+slot 내장 ID 반환. seq가 0으로 wrap되면 1로 재시작.
	[[nodiscard]] KeyId Next() noexcept
	{
		uint64_t seq = mSeq.fetch_add(1, std::memory_order_relaxed) + 1;
		seq &= kSeqMask;
		if (seq == 0) { seq = 1; }  // skip 0 (kInvalid sentinel)
		return mPrefix | seq;
	}

private:
	static constexpr uint64_t kSeqMask = (1ULL << 48) - 1;  // seq 비트 마스크 (하위 48비트)

	uint64_t              mPrefix;  // 생성자에서 미리 계산된 tag+slot 비트 (불변)
	std::atomic<uint64_t> mSeq;     // 인스턴스별 단조 증가 시퀀스 카운터 (thread-safe)

	// C++17 inline static — ODR-safe, no separate .cpp definition needed.
	// 한글: C++17 inline static — 헤더 정의로 ODR 안전.
	inline static std::atomic<KeyId> sGlobalSeq{0};  // Pattern B 전역 시퀀스 카운터 (thread-safe)
};

} // namespace Network::Utils
