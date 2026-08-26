#include "n64/rcp/rsp/rsp.hpp"

#include "n64/common/log.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>

namespace n64 {
namespace {

constexpr u64 kAccumulatorModulus = 1ull << 48;
constexpr u64 kAccumulatorMask = kAccumulatorModulus - 1;
constexpr u64 kAccumulatorSign = 1ull << 47;

constexpr std::array<std::array<u8, 8>, 16> kElementSelect{{
    {{0, 1, 2, 3, 4, 5, 6, 7}},
    {{0, 1, 2, 3, 4, 5, 6, 7}},
    {{0, 0, 2, 2, 4, 4, 6, 6}},
    {{1, 1, 3, 3, 5, 5, 7, 7}},
    {{0, 0, 0, 0, 4, 4, 4, 4}},
    {{1, 1, 1, 1, 5, 5, 5, 5}},
    {{2, 2, 2, 2, 6, 6, 6, 6}},
    {{3, 3, 3, 3, 7, 7, 7, 7}},
    {{0, 0, 0, 0, 0, 0, 0, 0}},
    {{1, 1, 1, 1, 1, 1, 1, 1}},
    {{2, 2, 2, 2, 2, 2, 2, 2}},
    {{3, 3, 3, 3, 3, 3, 3, 3}},
    {{4, 4, 4, 4, 4, 4, 4, 4}},
    {{5, 5, 5, 5, 5, 5, 5, 5}},
    {{6, 6, 6, 6, 6, 6, 6, 6}},
    {{7, 7, 7, 7, 7, 7, 7, 7}},
}};

[[nodiscard]] constexpr s16 signed_lane(u16 value) noexcept {
    return value < 0x8000u
        ? static_cast<s16>(value)
        : static_cast<s16>(static_cast<s32>(value) - 0x1'0000);
}

[[nodiscard]] constexpr u16 lane_bits(s64 value) noexcept {
    return static_cast<u16>(static_cast<u64>(value) & 0xFFFFu);
}

[[nodiscard]] constexpr s64 from_accumulator_bits(u64 raw) noexcept {
    raw &= kAccumulatorMask;
    if ((raw & kAccumulatorSign) == 0) {
        return static_cast<s64>(raw);
    }
    return -static_cast<s64>(kAccumulatorModulus - raw);
}

[[nodiscard]] constexpr s64 wrap_accumulator(s64 value) noexcept {
    return from_accumulator_bits(static_cast<u64>(value));
}

[[nodiscard]] constexpr s64 replace_accumulator_low(s64 acc, u16 low) noexcept {
    const u64 raw = (static_cast<u64>(acc) & kAccumulatorMask & ~0xFFFFull) | low;
    return from_accumulator_bits(raw);
}

[[nodiscard]] constexpr s64 accumulator_shift16(s64 acc) noexcept {
    if (acc >= 0) {
        return acc / 0x1'0000;
    }
    return -((-acc + 0xFFFF) / 0x1'0000);
}

[[nodiscard]] constexpr s64 arithmetic_shift1(s64 value) noexcept {
    return value >= 0 ? value / 2 : -((-value + 1) / 2);
}

[[nodiscard]] constexpr s64 replace_accumulator_upper(s64 acc, s32 upper) noexcept {
    const u64 raw = (static_cast<u64>(static_cast<u32>(upper)) << 16) |
                    (static_cast<u64>(acc) & 0xFFFFu);
    return from_accumulator_bits(raw);
}

[[nodiscard]] constexpr u16 signed_clamp(s64 value) noexcept {
    value = std::clamp(value,
                       static_cast<s64>(std::numeric_limits<s16>::min()),
                       static_cast<s64>(std::numeric_limits<s16>::max()));
    return lane_bits(value);
}

[[nodiscard]] constexpr u16 signed_accumulator_clamp(s64 acc) noexcept {
    return signed_clamp(accumulator_shift16(acc));
}

[[nodiscard]] constexpr u16 unsigned_accumulator_clamp(s64 acc) noexcept {
    const s64 value = std::clamp(accumulator_shift16(acc),
                                 static_cast<s64>(0), static_cast<s64>(0xFFFF));
    return static_cast<u16>(value);
}

[[nodiscard]] constexpr u16 accumulator_low_clamp(s64 acc) noexcept {
    const s64 middle = accumulator_shift16(acc);
    if (middle > std::numeric_limits<s16>::max()) {
        return 0xFFFFu;
    }
    if (middle < std::numeric_limits<s16>::min()) {
        return 0;
    }
    return lane_bits(acc);
}

[[nodiscard]] constexpr s32 sign_extend7(u32 value) noexcept {
    value &= 0x7Fu;
    return (value & 0x40u) != 0
        ? static_cast<s32>(value) - 0x80
        : static_cast<s32>(value);
}

[[nodiscard]] constexpr bool flag(u16 value, u32 lane, bool high = false) noexcept {
    return ((value >> (lane + (high ? 8u : 0u))) & 1u) != 0;
}

constexpr void set_flag(u16& value, u32 lane, bool enabled, bool high = false) noexcept {
    const u16 mask = static_cast<u16>(1u << (lane + (high ? 8u : 0u)));
    if (enabled) {
        value = static_cast<u16>(value | mask);
    } else {
        value = static_cast<u16>(value & static_cast<u16>(~mask));
    }
}

[[nodiscard]] constexpr u32 integer_square_root(u64 value) noexcept {
    u64 result = 0;
    u64 bit = 1ull << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return static_cast<u32>(result);
}

[[nodiscard]] constexpr u16 division_rom(u32 address) noexcept {
    u32 full = 0;
    if (address < 512) {
        full = static_cast<u32>((1ull << 26) / (address + 512u));
    } else {
        const u32 index = address - 512u;
        const u32 fraction = index >> 1;
        const u32 exponent = (index & 1u) != 0 ? 42u : 41u;
        full = integer_square_root((1ull << exponent) / (fraction + 256u));
    }
    full = std::min(full, 0x1FFFFu);
    return static_cast<u16>(full - 0x1'0000u);
}

[[nodiscard]] constexpr s32 signed_word(u32 value) noexcept {
    if (value <= 0x7FFF'FFFFu) {
        return static_cast<s32>(value);
    }
    return static_cast<s32>(-static_cast<s64>(0x1'0000'0000ull - value));
}

[[nodiscard]] constexpr s32 division_estimate(s32 input, bool square_root,
                                               bool double_precision) noexcept {
    u32 magnitude = 0;
    if (!double_precision) {
        magnitude = input < 0 ? static_cast<u32>(-input) : static_cast<u32>(input);
    } else if (input < 0) {
        magnitude = input >= -32768
            ? static_cast<u32>(-input)
            : ~static_cast<u32>(input);
    } else {
        magnitude = static_cast<u32>(input);
    }

    u32 shift = 0;
    u32 normalized = magnitude;
    if (magnitude == 0) {
        shift = double_precision ? 0u : 16u;
    } else {
        shift = static_cast<u32>(std::countl_zero(magnitude));
        normalized <<= shift;
    }

    u32 address = (normalized >> 22) & 0x1FFu;
    if (square_root) {
        address = (address & 0x1FEu) | 0x200u | (shift & 1u);
    }
    u32 output_shift = shift ^ 31u;
    if (square_root) {
        output_shift >>= 1;
    }
    u32 output = (0x4000'0000u | (static_cast<u32>(division_rom(address)) << 14)) >>
                 output_shift;
    if (input == 0) {
        output = 0x7FFF'FFFFu;
    } else if (input == -32768) {
        output = 0xFFFF'0000u;
    } else if (input < 0) {
        output = ~output;
    }
    return signed_word(output);
}

} // namespace

void Rsp::set_accumulator(std::size_t lane, s64 value) noexcept {
    accumulator_[lane & 7] = wrap_accumulator(value);
}

u8 Rsp::vector_byte(u32 reg, u32 byte) const noexcept {
    const u16 value = vpr_[reg & 31][(byte & 15) >> 1];
    return (byte & 1) == 0
        ? static_cast<u8>(value >> 8)
        : static_cast<u8>(value & 0xFFu);
}

void Rsp::set_vector_byte(u32 reg, u32 byte, u8 value) noexcept {
    u16& lane = vpr_[reg & 31][(byte & 15) >> 1];
    if ((byte & 1) == 0) {
        lane = static_cast<u16>((lane & 0x00FFu) | (static_cast<u16>(value) << 8));
    } else {
        lane = static_cast<u16>((lane & 0xFF00u) | value);
    }
}

void Rsp::exec_cop2(u32 insn) {
    const u32 subop = rs(insn);
    const u32 scalar = rt(insn);
    const u32 vector_reg = rd(insn);
    const u32 element = (insn >> 7) & 15u;

    if ((subop & 0x10u) != 0) {
        exec_vector(insn);
        return;
    }

    switch (subop) {
    case 0x00: { // MFC2: two adjacent vector bytes, sign-extended.
        const u16 value = static_cast<u16>(
            (static_cast<u16>(vector_byte(vector_reg, element)) << 8) |
            vector_byte(vector_reg, (element + 1) & 15u));
        write_gpr(scalar, static_cast<u32>(static_cast<s32>(signed_lane(value))));
        break;
    }
    case 0x02: { // CFC2
        u16 value = 0;
        switch (vector_reg & 3u) {
        case 0: value = vco_; break;
        case 1: value = vcc_; break;
        default: value = vce_; break;
        }
        write_gpr(scalar, static_cast<u32>(static_cast<s32>(signed_lane(value))));
        break;
    }
    case 0x04: { // MTC2: unlike MFC2, the second byte does not wrap at element 15.
        const u16 value = static_cast<u16>(gpr_[scalar]);
        set_vector_byte(vector_reg, element, static_cast<u8>(value >> 8));
        if (element != 15) {
            set_vector_byte(vector_reg, element + 1, static_cast<u8>(value));
        }
        break;
    }
    case 0x06: { // CTC2
        const u16 value = static_cast<u16>(gpr_[scalar]);
        switch (vector_reg & 3u) {
        case 0: vco_ = value; break;
        case 1: vcc_ = value; break;
        default: vce_ = static_cast<u8>(value); break;
        }
        break;
    }
    default:
        N64_WARN("RSP reserved COP2 transfer subop={:02X} insn={:08X}", subop, insn);
        break;
    }
}

void Rsp::exec_vector(u32 insn) {
    const u32 element = (insn >> 21) & 15u;
    const u32 vt = rt(insn);
    const u32 vs = rd(insn);
    const u32 vd = sa(insn);
    const u32 function = fn(insn);

    const auto source_s = vpr_[vs];
    std::array<u16, kVectorLaneCount> source_t{};
    std::array<u16, kVectorLaneCount> result{};
    for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
        source_t[lane] = vpr_[vt][kElementSelect[element][lane]];
    }

    bool write_result = true;
    switch (function) {
    case 0x02: // VRNDP
    case 0x0A: // VRNDN
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            s64 value = signed_lane(source_t[lane]);
            if ((vs & 1u) != 0) {
                value *= 0x1'0000;
            }
            const bool add = function == 0x02
                ? accumulator_[lane] >= 0
                : accumulator_[lane] < 0;
            if (add) {
                accumulator_[lane] = wrap_accumulator(accumulator_[lane] + value);
            }
            result[lane] = signed_accumulator_clamp(accumulator_[lane]);
        }
        break;

    case 0x03: // VMULQ
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            s32 product = static_cast<s32>(
                static_cast<s64>(signed_lane(source_s[lane])) * signed_lane(source_t[lane]));
            if (product < 0) {
                product += 31;
            }
            accumulator_[lane] = wrap_accumulator(static_cast<s64>(product) * 0x1'0000);
            result[lane] = static_cast<u16>(
                signed_clamp(arithmetic_shift1(product)) & 0xFFF0u);
        }
        break;

    case 0x0B: // VMACQ
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            const u64 raw = static_cast<u64>(accumulator_[lane]) & kAccumulatorMask;
            s32 upper = signed_word(static_cast<u32>(raw >> 16));
            if (upper < -0x20 && (upper & 0x20) == 0) {
                upper += 0x20;
            } else if (upper > 0x20 && (upper & 0x20) == 0) {
                upper -= 0x20;
            }
            accumulator_[lane] = replace_accumulator_upper(accumulator_[lane], upper);
            result[lane] = static_cast<u16>(
                signed_clamp(arithmetic_shift1(upper)) & 0xFFF0u);
        }
        break;

    case 0x00: // VMULF
    case 0x01: // VMULU
    case 0x04: // VMUDL
    case 0x05: // VMUDM
    case 0x06: // VMUDN
    case 0x07: // VMUDH
    case 0x08: // VMACF
    case 0x09: // VMACU
    case 0x0C: // VMADL
    case 0x0D: // VMADM
    case 0x0E: // VMADN
    case 0x0F: // VMADH
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            const s64 ss = signed_lane(source_s[lane]);
            const s64 st = signed_lane(source_t[lane]);
            const s64 us = source_s[lane];
            const s64 ut = source_t[lane];
            s64 acc = accumulator_[lane];
            switch (function) {
            case 0x00: acc = (ss * st * 2) + 0x8000; break;
            case 0x01: acc = (ss * st * 2) + 0x8000; break;
            case 0x04: acc = (us * ut) >> 16; break;
            case 0x05: acc = ss * ut; break;
            case 0x06: acc = us * st; break;
            case 0x07: acc = (ss * st) << 16; break;
            case 0x08: acc += ss * st * 2; break;
            case 0x09: acc += ss * st * 2; break;
            case 0x0C: acc += (us * ut) >> 16; break;
            case 0x0D: acc += ss * ut; break;
            case 0x0E: acc += us * st; break;
            case 0x0F: acc += (ss * st) << 16; break;
            default: break;
            }
            accumulator_[lane] = wrap_accumulator(acc);

            switch (function) {
            case 0x00:
            case 0x05:
            case 0x07:
            case 0x08:
            case 0x0D:
            case 0x0F:
                result[lane] = signed_accumulator_clamp(accumulator_[lane]);
                break;
            case 0x01:
            case 0x09:
                result[lane] = unsigned_accumulator_clamp(accumulator_[lane]);
                break;
            default:
                result[lane] = accumulator_low_clamp(accumulator_[lane]);
                break;
            }
        }
        break;

    case 0x10: // VADD
    case 0x11: // VSUB
    case 0x13: // VABS
    case 0x14: // VADDC
    case 0x15: // VSUBC
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            const s32 a = signed_lane(source_s[lane]);
            const s32 b = signed_lane(source_t[lane]);
            const bool carry = flag(vco_, lane);
            s64 arithmetic = 0;
            switch (function) {
            case 0x10:
                arithmetic = static_cast<s64>(a) + b + (carry ? 1 : 0);
                result[lane] = signed_clamp(arithmetic);
                break;
            case 0x11:
                arithmetic = static_cast<s64>(a) - b - (carry ? 1 : 0);
                result[lane] = signed_clamp(arithmetic);
                break;
            case 0x13:
                if (a < 0) {
                    arithmetic = b == std::numeric_limits<s16>::min()
                        ? std::numeric_limits<s16>::max()
                        : -b;
                } else if (a > 0) {
                    arithmetic = b;
                }
                result[lane] = lane_bits(arithmetic);
                break;
            case 0x14: {
                const u32 sum = static_cast<u32>(source_s[lane]) + source_t[lane];
                result[lane] = static_cast<u16>(sum);
                set_flag(vco_, lane, sum > 0xFFFFu);
                set_flag(vco_, lane, false, true);
                break;
            }
            case 0x15: {
                const s32 difference = static_cast<s32>(source_s[lane]) - source_t[lane];
                result[lane] = static_cast<u16>(difference);
                set_flag(vco_, lane, difference < 0);
                set_flag(vco_, lane, source_s[lane] != source_t[lane], true);
                break;
            }
            default: break;
            }
            accumulator_[lane] = replace_accumulator_low(accumulator_[lane], result[lane]);
        }
        if (function == 0x10 || function == 0x11) {
            vco_ = 0;
        }
        break;

    case 0x1D: { // VSAR: element 8/9/10 selects accumulator high/middle/low.
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            const u64 raw = static_cast<u64>(accumulator_[lane]) & kAccumulatorMask;
            if (element >= 8 && element <= 10) {
                const u32 shift = (10u - element) * 16u;
                result[lane] = static_cast<u16>(raw >> shift);
            } else {
                result[lane] = 0;
            }
        }
        break;
    }

    case 0x20: // VLT
    case 0x21: // VEQ
    case 0x22: // VNE
    case 0x23: // VGE
        vcc_ = 0;
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            const s16 a = signed_lane(source_s[lane]);
            const s16 b = signed_lane(source_t[lane]);
            const bool ne = flag(vco_, lane, true);
            const bool carry = flag(vco_, lane);
            bool condition = false;
            switch (function) {
            case 0x20: condition = a < b || (a == b && ne && carry); break;
            case 0x21: condition = a == b && !ne; break;
            case 0x22: condition = a != b || ne; break;
            case 0x23: condition = a > b || (a == b && !(ne && carry)); break;
            default: break;
            }
            set_flag(vcc_, lane, condition);
            result[lane] = condition ? source_s[lane] : source_t[lane];
            accumulator_[lane] = replace_accumulator_low(accumulator_[lane], result[lane]);
        }
        vco_ = 0;
        vce_ = 0;
        break;

    case 0x24: // VCL
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            const bool sign = flag(vco_, lane);
            const bool equal = !flag(vco_, lane, true);
            const u16 a = source_s[lane];
            const u16 b = source_t[lane];
            const u16 adjusted_b = sign ? static_cast<u16>(0u - b) : b;
            const u16 difference = static_cast<u16>(a - adjusted_b);
            const bool low_zero = difference == 0;
            const bool unsigned_zero = static_cast<u32>(a) + b < 0x1'0000u;
            const bool extended_low = (low_zero && unsigned_zero && (vce_ & (1u << lane)) == 0) ||
                                      ((low_zero || unsigned_zero) && (vce_ & (1u << lane)) != 0);
            const bool generated = a >= adjusted_b;
            const bool low = equal && sign ? extended_low : flag(vcc_, lane);
            const bool high = equal && !sign ? generated : flag(vcc_, lane, true);
            set_flag(vcc_, lane, low);
            set_flag(vcc_, lane, high, true);
            const bool condition = sign ? low : high;
            result[lane] = condition ? adjusted_b : a;
            accumulator_[lane] = replace_accumulator_low(accumulator_[lane], result[lane]);
        }
        vco_ = 0;
        vce_ = 0;
        break;

    case 0x25: // VCH
        vcc_ = 0;
        vco_ = 0;
        vce_ = 0;
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            const s16 a = signed_lane(source_s[lane]);
            const s16 b = signed_lane(source_t[lane]);
            const bool sign = ((source_s[lane] ^ source_t[lane]) & 0x8000u) != 0;
            const bool corner = b == std::numeric_limits<s16>::min();
            u16 adjusted_bits = sign ? static_cast<u16>(~source_t[lane]) : source_t[lane];
            const bool extension = sign && a == signed_lane(adjusted_bits);
            if (sign && !corner) {
                adjusted_bits = static_cast<u16>(adjusted_bits + 1u);
            }
            const s16 adjusted = signed_lane(adjusted_bits);
            const bool equal = (a == adjusted && !corner) || extension;
            const s16 signed_difference = signed_lane(
                static_cast<u16>(adjusted_bits - source_s[lane]));
            const bool low = sign ? signed_difference >= 0 : b < 0;
            const s16 high_left = sign ? static_cast<s16>(-1) : a;
            const bool high = high_left >= b;
            const bool condition = sign ? low : high;
            result[lane] = condition ? adjusted_bits : source_s[lane];
            set_flag(vcc_, lane, low);
            set_flag(vcc_, lane, high, true);
            set_flag(vco_, lane, sign);
            set_flag(vco_, lane, !equal, true);
            if (extension) {
                vce_ = static_cast<u8>(vce_ | static_cast<u8>(1u << lane));
            }
            accumulator_[lane] = replace_accumulator_low(accumulator_[lane], result[lane]);
        }
        break;

    case 0x26: // VCR
        vcc_ = 0;
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            const s16 b = signed_lane(source_t[lane]);
            const bool sign = ((source_s[lane] ^ source_t[lane]) & 0x8000u) != 0;
            const u16 sign_mask = sign ? 0xFFFFu : 0;
            const s16 low_limit = signed_lane(static_cast<u16>(~(source_s[lane] & sign_mask)));
            const s16 high_limit = signed_lane(static_cast<u16>(source_s[lane] | sign_mask));
            const bool low = b <= low_limit;
            const bool high = high_limit >= b;
            const bool condition = sign ? low : high;
            const u16 adjusted_b = static_cast<u16>(source_t[lane] ^ sign_mask);
            result[lane] = condition ? adjusted_b : source_s[lane];
            set_flag(vcc_, lane, low);
            set_flag(vcc_, lane, high, true);
            accumulator_[lane] = replace_accumulator_low(accumulator_[lane], result[lane]);
        }
        vco_ = 0;
        vce_ = 0;
        break;

    case 0x27: // VMRG
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            result[lane] = flag(vcc_, lane) ? source_s[lane] : source_t[lane];
            accumulator_[lane] = replace_accumulator_low(accumulator_[lane], result[lane]);
        }
        break;

    case 0x28: // VAND
    case 0x29: // VNAND
    case 0x2A: // VOR
    case 0x2B: // VNOR
    case 0x2C: // VXOR
    case 0x2D: // VNXOR
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            switch (function) {
            case 0x28: result[lane] = static_cast<u16>(source_s[lane] & source_t[lane]); break;
            case 0x29: result[lane] = static_cast<u16>(~(source_s[lane] & source_t[lane])); break;
            case 0x2A: result[lane] = static_cast<u16>(source_s[lane] | source_t[lane]); break;
            case 0x2B: result[lane] = static_cast<u16>(~(source_s[lane] | source_t[lane])); break;
            case 0x2C: result[lane] = static_cast<u16>(source_s[lane] ^ source_t[lane]); break;
            case 0x2D: result[lane] = static_cast<u16>(~(source_s[lane] ^ source_t[lane])); break;
            default: break;
            }
            accumulator_[lane] = replace_accumulator_low(accumulator_[lane], result[lane]);
        }
        break;

    case 0x30: // VRCP
    case 0x31: // VRCPL
    case 0x32: // VRCPH
    case 0x33: // VMOV
    case 0x34: // VRSQ
    case 0x35: // VRSQL
    case 0x36: { // VRSQH
        result = vpr_[vd];
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            accumulator_[lane] = replace_accumulator_low(accumulator_[lane], source_t[lane]);
        }

        const u32 destination_lane = vs & 7u;
        const u16 input_bits = vpr_[vt][element & 7u];
        if (function == 0x33) {
            result[destination_lane] = source_t[destination_lane];
            break;
        }

        if (function == 0x32 || function == 0x36) {
            result[destination_lane] = static_cast<u16>(
                static_cast<u32>(div_out_) >> 16);
            div_in_ = static_cast<s32>(signed_lane(input_bits)) * 0x1'0000;
            div_high_pending_ = true;
            break;
        }

        if (function == 0x31 || function == 0x35) {
            if (div_high_pending_) {
                div_in_ = signed_word(static_cast<u32>(div_in_) | input_bits);
            } else {
                div_in_ = signed_lane(input_bits);
            }
        } else {
            div_in_ = signed_lane(input_bits);
        }

        const bool square_root = function == 0x34 || function == 0x35;
        div_out_ = division_estimate(div_in_, square_root, div_high_pending_);
        result[destination_lane] = static_cast<u16>(div_out_);
        div_high_pending_ = false;
        break;
    }

    case 0x37: // VNOP
        write_result = false;
        break;

    default:
        N64_WARN("RSP reserved vector function={:02X} insn={:08X}", function, insn);
        write_result = false;
        break;
    }

    if (write_result) {
        vpr_[vd] = result;
    }
}

void Rsp::exec_vector_memory(u32 insn, bool store) {
    const u32 base = rs(insn);
    const u32 vt = rt(insn);
    const u32 subop = rd(insn);
    const u32 element = (insn >> 7) & 15u;
    const s32 offset = sign_extend7(insn);

    u32 scale = 0;
    switch (subop) {
    case 0: scale = 0; break;
    case 1: scale = 1; break;
    case 2: scale = 2; break;
    case 3: scale = 3; break;
    case 4:
    case 5: scale = 4; break;
    case 6:
    case 7: scale = 3; break;
    case 8:
    case 9:
    case 10:
    case 11: scale = 4; break;
    default:
        N64_WARN("RSP unsupported vector memory subop={:02X} insn={:08X}", subop, insn);
        return;
    }

    const s64 signed_address = static_cast<s64>(gpr_[base]) +
                               static_cast<s64>(offset) * (static_cast<s64>(1) << scale);
    const u32 address = static_cast<u32>(signed_address) & 0xFFFu;

    if (subop == 6 || subop == 7) { // LPV/SPV and LUV/SUV
        if (element != 0) {
            N64_WARN("RSP packed vector memory op with unsupported element={}", element);
            return;
        }
        const u32 shift = subop == 6 ? 8u : 7u;
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            if (store) {
                store_byte(address + lane, static_cast<u8>(vpr_[vt][lane] >> shift));
            } else {
                vpr_[vt][lane] = static_cast<u16>(load_byte(address + lane) << shift);
            }
        }
        return;
    }

    if (subop == 8) { // LHV/SHV: eight bytes at a two-byte stride.
        if (element != 0) {
            N64_WARN("RSP half-packed vector memory op with unsupported element={}", element);
            return;
        }
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            if (store) {
                store_byte(address + lane * 2u, static_cast<u8>(vpr_[vt][lane] >> 7));
            } else {
                vpr_[vt][lane] = static_cast<u16>(load_byte(address + lane * 2u) << 7);
            }
        }
        return;
    }

    if (subop == 9) { // LFV/SFV: four fractional lanes at a four-byte stride.
        const u32 first_lane = (element >> 1) & 7u;
        const u32 count = std::min(4u, 8u - first_lane);
        for (u32 i = 0; i < count; ++i) {
            if (store) {
                store_byte(address + i * 4u,
                           static_cast<u8>(vpr_[vt][first_lane + i] >> 7));
            } else {
                vpr_[vt][first_lane + i] =
                    static_cast<u16>(load_byte(address + i * 4u) << 7);
            }
        }
        return;
    }

    if (subop == 10) { // LWV is reserved; SWV writes a rotated full vector.
        if (!store) {
            N64_WARN("RSP reserved LWV insn={:08X}", insn);
            return;
        }
        for (u32 i = 0; i < 16; ++i) {
            store_byte(address + i, vector_byte(vt, (element + i) & 15u));
        }
        return;
    }

    if (subop == 11) { // LTV/STV matrix transpose across an eight-register bank.
        for (u32 lane = 0; lane < kVectorLaneCount; ++lane) {
            if (store) {
                const u32 reg = (vt + ((element >> 1) + lane) % 8u) & 31u;
                store_half(address + lane * 2u, vpr_[reg][lane]);
            } else {
                const u32 reg = (vt + lane) & 31u;
                const u32 destination_lane = (lane + 8u - (element >> 1)) & 7u;
                vpr_[reg][destination_lane] =
                    static_cast<u16>(load_half(address + lane * 2u));
            }
        }
        return;
    }

    if (subop <= 3) {
        const u32 count = 1u << subop;
        for (u32 i = 0; i < count && element + i < 16; ++i) {
            if (store) {
                store_byte(address + i, vector_byte(vt, element + i));
            } else {
                set_vector_byte(vt, element + i, static_cast<u8>(load_byte(address + i)));
            }
        }
        return;
    }

    if (subop == 4) { // LQV/SQV: transfer until the next 16-byte DMEM boundary.
        const u32 count = std::min(16u - (address & 15u), 16u - element);
        for (u32 i = 0; i < count; ++i) {
            if (store) {
                store_byte(address + i, vector_byte(vt, element + i));
            } else {
                set_vector_byte(vt, element + i, static_cast<u8>(load_byte(address + i)));
            }
        }
        return;
    }

    // LRV/SRV: the bytes before the address fill the right side of the vector.
    const u32 count = address & 15u;
    const u32 aligned = address & ~15u;
    const u32 vector_start = 16u - count;
    for (u32 i = 0; i < count; ++i) {
        const u32 vector_index = (element + vector_start + i) & 15u;
        if (store) {
            store_byte(aligned + i, vector_byte(vt, vector_index));
        } else {
            set_vector_byte(vt, vector_index, static_cast<u8>(load_byte(aligned + i)));
        }
    }
}

} // namespace n64
