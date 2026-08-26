#include "n64/cpu/cpu.hpp"

#include <bit>
#include <cfenv>
#include <cmath>
#include <limits>
#include <type_traits>

namespace n64 {
namespace {

constexpr u32 kStatusFr = 1u << 26;

constexpr u32 kFcr31RmMask = 0x0000'0003u;
constexpr u32 kFcr31FlagInexact = 1u << 2;
constexpr u32 kFcr31FlagUnderflow = 1u << 3;
constexpr u32 kFcr31FlagOverflow = 1u << 4;
constexpr u32 kFcr31FlagDivZero = 1u << 5;
constexpr u32 kFcr31FlagInvalid = 1u << 6;
constexpr u32 kFcr31FlagMask = 0x0000'007Cu;
constexpr u32 kFcr31EnableMask = 0x0000'0F80u;
constexpr u32 kFcr31CauseMask = 0x0003'F000u;
constexpr u32 kFcr31CauseUnimplemented = 1u << 17;
constexpr u32 kFcr31Condition = 1u << 23;
constexpr u32 kFcr31FlushSubnormals = 1u << 24;
constexpr u32 kFcr31WritableMask = 0x0183'FFFFu;
constexpr u32 kFcr0Implementation = 0x0000'0B00u;

constexpr u32 kFmtSingle = 0x10;
constexpr u32 kFmtDouble = 0x11;
constexpr u32 kFmtWord = 0x14;
constexpr u32 kFmtLong = 0x15;

constexpr u32 kCanonicalQuietNan32 = 0x7FBF'FFFFu;
constexpr u64 kCanonicalQuietNan64 = 0x7FF7'FFFF'FFFF'FFFFull;

enum class InputIssue {
    None,
    Invalid,
    Unimplemented,
};

[[nodiscard]] bool is_nan(u32 bits) noexcept {
    return (bits & 0x7F80'0000u) == 0x7F80'0000u &&
           (bits & 0x007F'FFFFu) != 0;
}

[[nodiscard]] bool is_nan(u64 bits) noexcept {
    return (bits & 0x7FF0'0000'0000'0000ull) == 0x7FF0'0000'0000'0000ull &&
           (bits & 0x000F'FFFF'FFFF'FFFFull) != 0;
}

// The VR4300 follows the legacy MIPS NaN encoding: the most-significant
// fraction bit is one for a signaling NaN and zero for a quiet NaN.
[[nodiscard]] bool is_signaling_nan(u32 bits) noexcept {
    return is_nan(bits) && (bits & 0x0040'0000u) != 0;
}

[[nodiscard]] bool is_signaling_nan(u64 bits) noexcept {
    return is_nan(bits) && (bits & 0x0008'0000'0000'0000ull) != 0;
}

[[nodiscard]] bool is_subnormal(u32 bits) noexcept {
    return (bits & 0x7F80'0000u) == 0 && (bits & 0x007F'FFFFu) != 0;
}

[[nodiscard]] bool is_subnormal(u64 bits) noexcept {
    return (bits & 0x7FF0'0000'0000'0000ull) == 0 &&
           (bits & 0x000F'FFFF'FFFF'FFFFull) != 0;
}

[[nodiscard]] InputIssue classify_computational_operand(u32 bits) noexcept {
    if (is_signaling_nan(bits)) return InputIssue::Invalid;
    if (is_nan(bits) || is_subnormal(bits)) return InputIssue::Unimplemented;
    return InputIssue::None;
}

[[nodiscard]] InputIssue classify_computational_operand(u64 bits) noexcept {
    if (is_signaling_nan(bits)) return InputIssue::Invalid;
    if (is_nan(bits) || is_subnormal(bits)) return InputIssue::Unimplemented;
    return InputIssue::None;
}

[[nodiscard]] int host_rounding_mode(u32 rm) noexcept {
    switch (rm & kFcr31RmMask) {
    case 1: return FE_TOWARDZERO;
    case 2: return FE_UPWARD;
    case 3: return FE_DOWNWARD;
    default: return FE_TONEAREST;
    }
}

class ScopedFpEnvironment {
public:
    explicit ScopedFpEnvironment(u32 rm) noexcept {
        (void)std::feholdexcept(&saved_);
        (void)std::fesetround(host_rounding_mode(rm));
    }

    ScopedFpEnvironment(const ScopedFpEnvironment&) = delete;
    ScopedFpEnvironment& operator=(const ScopedFpEnvironment&) = delete;

    ~ScopedFpEnvironment() {
        (void)std::fesetenv(&saved_);
    }

    [[nodiscard]] u32 flags() const noexcept {
        const int host = std::fetestexcept(
            FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT);
        u32 result = 0;
        if ((host & FE_INVALID) != 0) result |= kFcr31FlagInvalid;
        if ((host & FE_DIVBYZERO) != 0) result |= kFcr31FlagDivZero;
        if ((host & FE_OVERFLOW) != 0) result |= kFcr31FlagOverflow;
        if ((host & FE_UNDERFLOW) != 0) result |= kFcr31FlagUnderflow;
        if ((host & FE_INEXACT) != 0) result |= kFcr31FlagInexact;
        if ((result & kFcr31FlagOverflow) != 0) result |= kFcr31FlagInexact;
        return result;
    }

private:
    std::fenv_t saved_{};
};

template <typename Float>
struct ArithmeticResult {
    Float value{};
    u32 flags = 0;
};

template <typename Float>
[[nodiscard]] ArithmeticResult<Float> calculate_arithmetic(
    u32 function, Float left, Float right, u32 rm) noexcept {
    ScopedFpEnvironment environment(rm);
    const volatile Float lhs = left;
    const volatile Float rhs = right;
    volatile Float result = static_cast<Float>(0);

    switch (function) {
    case 0x00: result = lhs + rhs; break;
    case 0x01: result = lhs - rhs; break;
    case 0x02: result = lhs * rhs; break;
    case 0x03: result = lhs / rhs; break;
    case 0x04: result = std::sqrt(lhs); break;
    default: break;
    }

    u32 flags = environment.flags();
    if (function == 0x03 && rhs == static_cast<Float>(0)) {
        if (lhs == static_cast<Float>(0) || std::isnan(left)) {
            flags |= kFcr31FlagInvalid;
            flags &= ~kFcr31FlagDivZero;
        } else if (std::isfinite(left)) {
            flags |= kFcr31FlagDivZero;
        }
    }
    if (function == 0x04 && lhs < static_cast<Float>(0)) {
        flags |= kFcr31FlagInvalid;
    }
    if (function == 0x02 &&
        ((std::isinf(left) && right == static_cast<Float>(0)) ||
         (std::isinf(right) && left == static_cast<Float>(0)))) {
        flags |= kFcr31FlagInvalid;
    }
    if (std::isinf(static_cast<Float>(result)) && std::isfinite(left) &&
        std::isfinite(right) &&
        (function != 0x03u || rhs != static_cast<Float>(0))) {
        flags |= kFcr31FlagOverflow | kFcr31FlagInexact;
    }

    return {static_cast<Float>(result), flags};
}

[[nodiscard]] long double rounded_integer(long double value, u32 rm) noexcept {
    switch (rm & kFcr31RmMask) {
    case 1:
        return std::trunc(value);
    case 2:
        return std::ceil(value);
    case 3:
        return std::floor(value);
    default: {
        const long double lower = std::floor(value);
        const long double fraction = value - lower;
        if (fraction < 0.5L) return lower;
        if (fraction > 0.5L) return lower + 1.0L;
        return std::fmod(lower, 2.0L) == 0.0L ? lower : lower + 1.0L;
    }
    }
}

template <typename Integer>
struct IntegerConversion {
    Integer value{};
    u32 flags = 0;
};

template <typename Integer, typename Float>
[[nodiscard]] IntegerConversion<Integer> convert_to_integer(Float source, u32 rm) noexcept {
    static_assert(std::is_same_v<Integer, s32> || std::is_same_v<Integer, s64>);
    const long double value = static_cast<long double>(source);
    if (!std::isfinite(value)) {
        return {std::numeric_limits<Integer>::min(), kFcr31FlagInvalid};
    }

    const long double rounded = rounded_integer(value, rm);
    constexpr long double minimum = static_cast<long double>(
        std::numeric_limits<Integer>::min());
    const long double exclusive_maximum = -minimum;
    if (rounded < minimum || rounded >= exclusive_maximum) {
        return {std::numeric_limits<Integer>::min(), kFcr31FlagInvalid};
    }

    u32 flags = 0;
    if (rounded != value) flags |= kFcr31FlagInexact;
    return {static_cast<Integer>(rounded), flags};
}

template <typename Destination, typename Source>
[[nodiscard]] ArithmeticResult<Destination> convert_floating(
    Source source, u32 rm) noexcept {
    ScopedFpEnvironment environment(rm);
    const volatile Source input = source;
    const volatile Destination output = static_cast<Destination>(input);
    u32 flags = environment.flags();
    if (std::isfinite(source) && std::isinf(static_cast<Destination>(output))) {
        flags |= kFcr31FlagOverflow | kFcr31FlagInexact;
    }
    return {static_cast<Destination>(output), flags};
}

} // namespace

bool Cpu::fpu_fr_mode() const noexcept {
    return (cop0_[Cop0Reg::Status] & kStatusFr) != 0;
}

u32 Cpu::read_fpr_word(u32 index) const noexcept {
    return static_cast<u32>(fpr_[index & 31u]);
}

void Cpu::write_fpr_word(u32 index, u32 value) noexcept {
    u64& reg = fpr_[index & 31u];
    reg = (reg & 0xFFFF'FFFF'0000'0000ull) | value;
}

bool Cpu::read_fpr_double(u32 index, u64& value) const noexcept {
    index &= 31u;
    if (fpu_fr_mode()) {
        value = fpr_[index];
        return true;
    }
    if ((index & 1u) != 0) return false;
    value = static_cast<u64>(read_fpr_word(index)) |
            (static_cast<u64>(read_fpr_word(index + 1u)) << 32);
    return true;
}

bool Cpu::write_fpr_double(u32 index, u64 value) noexcept {
    index &= 31u;
    if (fpu_fr_mode()) {
        fpr_[index] = value;
        return true;
    }
    if ((index & 1u) != 0) return false;
    write_fpr_word(index, static_cast<u32>(value));
    write_fpr_word(index + 1u, static_cast<u32>(value >> 32));
    return true;
}

bool Cpu::finish_fpu_operation(u32 flags) {
    flags &= kFcr31FlagMask;
    fcr31_ = (fcr31_ & ~kFcr31CauseMask) | (flags << 10);

    const u32 enabled = (fcr31_ & kFcr31EnableMask) >> 5;
    if ((flags & enabled) != 0) {
        raise_exception(ExcCode::FPE);
        return false;
    }

    fcr31_ |= flags;
    return true;
}

void Cpu::raise_fpu_unimplemented() {
    fcr31_ = (fcr31_ & ~kFcr31CauseMask) | kFcr31CauseUnimplemented;
    raise_exception(ExcCode::FPE);
}

void Cpu::exec_cop1(u32 insn) {
    if (!coprocessor_usable(1)) {
        raise_exception(ExcCode::CpU, 0, 1);
        return;
    }

    const u32 cop1_rs = rs(insn);
    const u32 cop1_rt = rt(insn);
    const u32 fs = rd(insn);

    switch (cop1_rs) {
    case 0x00: // MFC1
        write_gpr(cop1_rt, sext32(read_fpr_word(fs)));
        break;
    case 0x01: { // DMFC1
        u64 value = 0;
        if (!read_fpr_double(fs, value)) {
            raise_fpu_unimplemented();
        } else {
            write_gpr(cop1_rt, value);
        }
        break;
    }
    case 0x02: { // CFC1
        u32 value = 0;
        if (fs == 0) value = kFcr0Implementation;
        if (fs == 31) value = fcr31_;
        write_gpr(cop1_rt, sext32(value));
        break;
    }
    case 0x04: // MTC1
        write_fpr_word(fs, static_cast<u32>(gpr_[cop1_rt]));
        break;
    case 0x05: // DMTC1
        if (!write_fpr_double(fs, gpr_[cop1_rt])) {
            raise_fpu_unimplemented();
        }
        break;
    case 0x06: // CTC1
        if (fs == 31) {
            fcr31_ = static_cast<u32>(gpr_[cop1_rt]) & kFcr31WritableMask;
            const u32 causes = (fcr31_ >> 10) & kFcr31FlagMask;
            const u32 enables = (fcr31_ >> 5) & kFcr31FlagMask;
            if ((fcr31_ & kFcr31CauseUnimplemented) != 0 ||
                (causes & enables) != 0) {
                raise_exception(ExcCode::FPE);
            }
        }
        break;
    case 0x08: { // BC1F, BC1T, BC1FL, BC1TL
        if (cop1_rt > 3u) {
            raise_fpu_unimplemented();
            break;
        }
        const bool condition = (fcr31_ & kFcr31Condition) != 0;
        const bool branch_on_true = (cop1_rt & 1u) != 0;
        const bool likely = (cop1_rt & 2u) != 0;
        if (condition == branch_on_true) {
            branch_rel(simm(insn));
        } else if (likely) {
            next_pc_ = pc_ + 8;
            branch_pending_ = false;
        } else {
            branch_abs(pc_ + 8);
        }
        break;
    }
    case kFmtSingle:
    case kFmtDouble:
    case kFmtWord:
    case kFmtLong:
        exec_cop1_format(insn);
        break;
    default:
        raise_fpu_unimplemented();
        break;
    }
}

void Cpu::exec_cop1_format(u32 insn) {
    const u32 format = rs(insn);
    const u32 ft = rt(insn);
    const u32 fs = rd(insn);
    const u32 fd = sa(insn);
    const u32 function = fn(insn);
    const u32 rounding_mode = fcr31_ & kFcr31RmMask;

    const auto handle_single_result = [this, fd, rounding_mode](float value, u32 flags) {
        u32 raw = std::bit_cast<u32>(value);
        if ((flags & kFcr31FlagInvalid) != 0) raw = kCanonicalQuietNan32;
        if ((flags & kFcr31FlagOverflow) != 0) {
            const u32 sign = raw & 0x8000'0000u;
            const bool toward_infinity =
                rounding_mode == 0u ||
                (rounding_mode == 2u && sign == 0) ||
                (rounding_mode == 3u && sign != 0);
            raw = sign | (toward_infinity ? 0x7F80'0000u : 0x7F7F'FFFFu);
        }
        if ((flags & kFcr31FlagUnderflow) != 0 || is_subnormal(raw)) {
            const bool flush = (fcr31_ & kFcr31FlushSubnormals) != 0;
            const bool underflow_enabled =
                (fcr31_ & ((1u << 8) | (1u << 7))) != 0;
            if (!flush || underflow_enabled) {
                raise_fpu_unimplemented();
                return;
            }
            const u32 sign = raw & 0x8000'0000u;
            const bool toward_minimum_normal =
                (rounding_mode == 2u && sign == 0) ||
                (rounding_mode == 3u && sign != 0);
            raw = sign | (toward_minimum_normal ? 0x0080'0000u : 0u);
            flags |= kFcr31FlagUnderflow | kFcr31FlagInexact;
        }
        if (finish_fpu_operation(flags)) write_fpr_word(fd, raw);
    };

    const auto handle_double_result = [this, fd, rounding_mode](double value, u32 flags) {
        u64 raw = std::bit_cast<u64>(value);
        if ((flags & kFcr31FlagInvalid) != 0) raw = kCanonicalQuietNan64;
        if ((flags & kFcr31FlagOverflow) != 0) {
            const u64 sign = raw & 0x8000'0000'0000'0000ull;
            const bool toward_infinity =
                rounding_mode == 0u ||
                (rounding_mode == 2u && sign == 0) ||
                (rounding_mode == 3u && sign != 0);
            raw = sign | (toward_infinity ? 0x7FF0'0000'0000'0000ull
                                          : 0x7FEF'FFFF'FFFF'FFFFull);
        }
        if ((flags & kFcr31FlagUnderflow) != 0 || is_subnormal(raw)) {
            const bool flush = (fcr31_ & kFcr31FlushSubnormals) != 0;
            const bool underflow_enabled =
                (fcr31_ & ((1u << 8) | (1u << 7))) != 0;
            if (!flush || underflow_enabled) {
                raise_fpu_unimplemented();
                return;
            }
            const u64 sign = raw & 0x8000'0000'0000'0000ull;
            const bool toward_minimum_normal =
                (rounding_mode == 2u && sign == 0) ||
                (rounding_mode == 3u && sign != 0);
            raw = sign | (toward_minimum_normal
                              ? 0x0010'0000'0000'0000ull
                              : 0ull);
            flags |= kFcr31FlagUnderflow | kFcr31FlagInexact;
        }
        if (!fpu_fr_mode() && (fd & 1u) != 0) {
            raise_fpu_unimplemented();
            return;
        }
        if (finish_fpu_operation(flags)) (void)write_fpr_double(fd, raw);
    };

    const auto handle_input_issue = [this](InputIssue issue) {
        if (issue == InputIssue::Unimplemented) {
            raise_fpu_unimplemented();
            return false;
        }
        return true;
    };

    if (function <= 0x07u && (format == kFmtSingle || format == kFmtDouble)) {
        if (format == kFmtSingle) {
            const u32 fs_raw = read_fpr_word(fs);
            if (function == 0x06u) { // MOV does not change FCR31 causes.
                write_fpr_word(fd, fs_raw);
                return;
            }

            InputIssue issue = classify_computational_operand(fs_raw);
            if (function <= 0x03u) {
                const InputIssue ft_issue = classify_computational_operand(read_fpr_word(ft));
                if (issue == InputIssue::None || ft_issue == InputIssue::Unimplemented) {
                    issue = ft_issue;
                }
            }
            if (!handle_input_issue(issue)) return;
            if (issue == InputIssue::Invalid) {
                handle_single_result(std::bit_cast<float>(kCanonicalQuietNan32),
                                     kFcr31FlagInvalid);
                return;
            }

            if (function == 0x05u) {
                if (finish_fpu_operation(0)) write_fpr_word(fd, fs_raw & 0x7FFF'FFFFu);
                return;
            }
            if (function == 0x07u) {
                if (finish_fpu_operation(0)) write_fpr_word(fd, fs_raw ^ 0x8000'0000u);
                return;
            }

            const float left = std::bit_cast<float>(fs_raw);
            const float right = std::bit_cast<float>(read_fpr_word(ft));
            const auto result = calculate_arithmetic(function, left, right, rounding_mode);
            handle_single_result(result.value, result.flags);
            return;
        }

        u64 fs_raw = 0;
        u64 ft_raw = 0;
        if (!read_fpr_double(fs, fs_raw) ||
            (function <= 0x03u && !read_fpr_double(ft, ft_raw))) {
            raise_fpu_unimplemented();
            return;
        }
        if (function == 0x06u) {
            if (!write_fpr_double(fd, fs_raw)) raise_fpu_unimplemented();
            return;
        }

        InputIssue issue = classify_computational_operand(fs_raw);
        if (function <= 0x03u) {
            const InputIssue ft_issue = classify_computational_operand(ft_raw);
            if (issue == InputIssue::None || ft_issue == InputIssue::Unimplemented) {
                issue = ft_issue;
            }
        }
        if (!handle_input_issue(issue)) return;
        if (issue == InputIssue::Invalid) {
            handle_double_result(std::bit_cast<double>(kCanonicalQuietNan64),
                                 kFcr31FlagInvalid);
            return;
        }

        if (function == 0x05u) {
            handle_double_result(std::bit_cast<double>(fs_raw & 0x7FFF'FFFF'FFFF'FFFFull), 0);
            return;
        }
        if (function == 0x07u) {
            handle_double_result(std::bit_cast<double>(fs_raw ^ 0x8000'0000'0000'0000ull), 0);
            return;
        }

        const double left = std::bit_cast<double>(fs_raw);
        const double right = std::bit_cast<double>(ft_raw);
        const auto result = calculate_arithmetic(function, left, right, rounding_mode);
        handle_double_result(result.value, result.flags);
        return;
    }

    if ((function & 0x30u) == 0x30u &&
        (format == kFmtSingle || format == kFmtDouble)) {
        const u32 condition_code = function & 0x0Fu;
        bool unordered = false;
        bool equal = false;
        bool less = false;
        bool invalid = false;

        if (format == kFmtSingle) {
            const u32 fs_raw = read_fpr_word(fs);
            const u32 ft_raw = read_fpr_word(ft);
            unordered = is_nan(fs_raw) || is_nan(ft_raw);
            invalid = is_signaling_nan(fs_raw) || is_signaling_nan(ft_raw) ||
                      (unordered && (condition_code & 8u) != 0);
            if (!unordered) {
                const float left = std::bit_cast<float>(fs_raw);
                const float right = std::bit_cast<float>(ft_raw);
                equal = left == right;
                less = left < right;
            }
        } else {
            u64 fs_raw = 0;
            u64 ft_raw = 0;
            if (!read_fpr_double(fs, fs_raw) || !read_fpr_double(ft, ft_raw)) {
                raise_fpu_unimplemented();
                return;
            }
            unordered = is_nan(fs_raw) || is_nan(ft_raw);
            invalid = is_signaling_nan(fs_raw) || is_signaling_nan(ft_raw) ||
                      (unordered && (condition_code & 8u) != 0);
            if (!unordered) {
                const double left = std::bit_cast<double>(fs_raw);
                const double right = std::bit_cast<double>(ft_raw);
                equal = left == right;
                less = left < right;
            }
        }

        const u32 flags = invalid ? kFcr31FlagInvalid : 0u;
        if (!finish_fpu_operation(flags)) return;
        const bool result = (unordered && (condition_code & 1u) != 0) ||
                            (equal && (condition_code & 2u) != 0) ||
                            (less && (condition_code & 4u) != 0);
        if (result) {
            fcr31_ |= kFcr31Condition;
        } else {
            fcr31_ &= ~kFcr31Condition;
        }
        return;
    }

    const bool integer_conversion =
        (function >= 0x08u && function <= 0x0Fu) ||
        function == 0x24u || function == 0x25u;
    if (integer_conversion) {
        if (format != kFmtSingle && format != kFmtDouble) {
            raise_fpu_unimplemented();
            return;
        }

        long double source = 0;
        InputIssue issue = InputIssue::None;
        if (format == kFmtSingle) {
            const u32 raw = read_fpr_word(fs);
            issue = classify_computational_operand(raw);
            source = static_cast<long double>(std::bit_cast<float>(raw));
        } else {
            u64 raw = 0;
            if (!read_fpr_double(fs, raw)) {
                raise_fpu_unimplemented();
                return;
            }
            issue = classify_computational_operand(raw);
            source = static_cast<long double>(std::bit_cast<double>(raw));
        }
        if (!handle_input_issue(issue)) return;

        u32 operation_rm = rounding_mode;
        if (function >= 0x08u && function <= 0x0Fu) {
            operation_rm = (function - 0x08u) & 3u;
        }
        const bool to_word =
            (function >= 0x0Cu && function <= 0x0Fu) || function == 0x24u;
        if (to_word) {
            IntegerConversion<s32> result;
            if (issue == InputIssue::Invalid) {
                result = {std::numeric_limits<s32>::min(), kFcr31FlagInvalid};
            } else {
                result = convert_to_integer<s32>(source, operation_rm);
            }
            if (finish_fpu_operation(result.flags)) {
                write_fpr_word(fd, static_cast<u32>(result.value));
            }
        } else {
            if (!fpu_fr_mode() && (fd & 1u) != 0) {
                raise_fpu_unimplemented();
                return;
            }
            IntegerConversion<s64> result;
            if (issue == InputIssue::Invalid) {
                result = {std::numeric_limits<s64>::min(), kFcr31FlagInvalid};
            } else {
                result = convert_to_integer<s64>(source, operation_rm);
            }
            if (finish_fpu_operation(result.flags)) {
                (void)write_fpr_double(fd, static_cast<u64>(result.value));
            }
        }
        return;
    }

    if (function == 0x20u) { // CVT.S
        if (format == kFmtDouble) {
            u64 raw = 0;
            if (!read_fpr_double(fs, raw)) {
                raise_fpu_unimplemented();
                return;
            }
            const InputIssue issue = classify_computational_operand(raw);
            if (!handle_input_issue(issue)) return;
            if (issue == InputIssue::Invalid) {
                handle_single_result(std::bit_cast<float>(kCanonicalQuietNan32),
                                     kFcr31FlagInvalid);
                return;
            }
            const auto result = convert_floating<float>(std::bit_cast<double>(raw),
                                                        rounding_mode);
            handle_single_result(result.value, result.flags);
            return;
        }
        if (format == kFmtWord) {
            const s32 source = static_cast<s32>(read_fpr_word(fs));
            const auto result = convert_floating<float>(source, rounding_mode);
            handle_single_result(result.value, result.flags);
            return;
        }
        if (format == kFmtLong) {
            u64 raw = 0;
            if (!read_fpr_double(fs, raw)) {
                raise_fpu_unimplemented();
                return;
            }
            const auto result = convert_floating<float>(static_cast<s64>(raw),
                                                        rounding_mode);
            handle_single_result(result.value, result.flags);
            return;
        }
        raise_fpu_unimplemented();
        return;
    }

    if (function == 0x21u) { // CVT.D
        if (!fpu_fr_mode() && (fd & 1u) != 0) {
            raise_fpu_unimplemented();
            return;
        }
        if (format == kFmtSingle) {
            const u32 raw = read_fpr_word(fs);
            const InputIssue issue = classify_computational_operand(raw);
            if (!handle_input_issue(issue)) return;
            if (issue == InputIssue::Invalid) {
                handle_double_result(std::bit_cast<double>(kCanonicalQuietNan64),
                                     kFcr31FlagInvalid);
                return;
            }
            const auto result = convert_floating<double>(std::bit_cast<float>(raw),
                                                         rounding_mode);
            handle_double_result(result.value, result.flags);
            return;
        }
        if (format == kFmtWord) {
            const s32 source = static_cast<s32>(read_fpr_word(fs));
            const auto result = convert_floating<double>(source, rounding_mode);
            handle_double_result(result.value, result.flags);
            return;
        }
        if (format == kFmtLong) {
            u64 raw = 0;
            if (!read_fpr_double(fs, raw)) {
                raise_fpu_unimplemented();
                return;
            }
            const auto result = convert_floating<double>(static_cast<s64>(raw),
                                                         rounding_mode);
            handle_double_result(result.value, result.flags);
            return;
        }
        raise_fpu_unimplemented();
        return;
    }

    raise_fpu_unimplemented();
}

} // namespace n64
