#include "../../terms/integers.hpp"
#include "../typed.hpp"
#include "terms.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <ctime>

// The time builtins of plan step 41F (docs/builtins.md#time): monotonic, system and calendar time, time units and
// unique integers. Monotonic time is the steady clock, so start_timer/4 with {abs, true} shares its origin. The
// native and perf_counter units are nanoseconds.
namespace clause::runtime::builtins {
namespace {
using detail::Integer;
using std::chrono::days;
using std::chrono::duration_cast;
using std::chrono::nanoseconds;
using std::chrono::seconds;

// Parts per second of a time unit argument; badarg for anything else.
Integer unit_rate(const Term &unit) {
    if (const auto parts = small(unit.word()); parts && *parts > 0) {
        return *parts;
    }
    static constexpr std::array<std::pair<std::string_view, std::int64_t>, 10> UNITS{{
        {"second", 1},
        {"millisecond", 1'000},
        {"microsecond", 1'000'000},
        {"nanosecond", 1'000'000'000},
        {"native", 1'000'000'000},
        {"perf_counter", 1'000'000'000},
        {"seconds", 1},
        {"milli_seconds", 1'000},
        {"micro_seconds", 1'000'000},
        {"nano_seconds", 1'000'000'000},
    }};
    const auto name = unit.is_atom() ? need(unit.atom_spelling()) : std::string_view{};
    const auto found = std::ranges::find(UNITS, name, &std::pair<std::string_view, std::int64_t>::first);
    if (found == UNITS.end()) {
        bad_argument();
    }
    return found->second;
}

// The parts per second of a conversion's source and target units.
struct Rates {
    Integer from;
    Integer to;
};

// Time * To / From rounded down, as convert_time_unit/3 does.
Integer convert(const Integer &time, const Rates &rates) {
    const Integer scaled = time * rates.to;
    Integer quotient = scaled / rates.from;
    if (scaled % rates.from != 0 && scaled < 0) {
        quotient -= 1;
    }
    return quotient;
}

// A nanosecond count as an integer term in `unit`.
TermResult<Term> in_unit(ProcessContext &context, std::int64_t nanos, const Term &unit) {
    return detail::IntegerAccess::make(context.heap(), convert(nanos, {1'000'000'000, unit_rate(unit)}));
}

std::int64_t monotonic_nanos() {
    return duration_cast<nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::int64_t system_nanos() {
    return duration_cast<nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

TermResult<Term> monotonic_time0(ProcessContext &context) { return TermFactory(context).integer(monotonic_nanos()); }

TermResult<Term> monotonic_time1(ProcessContext &context, const Term &unit) {
    return in_unit(context, monotonic_nanos(), unit);
}

TermResult<Term> system_time0(ProcessContext &context) { return TermFactory(context).integer(system_nanos()); }

TermResult<Term> system_time1(ProcessContext &context, const Term &unit) {
    return in_unit(context, system_nanos(), unit);
}

TermResult<Term> time_offset0(ProcessContext &context) {
    return TermFactory(context).integer(system_nanos() - monotonic_nanos());
}

TermResult<Term> time_offset1(ProcessContext &context, const Term &unit) {
    return in_unit(context, system_nanos() - monotonic_nanos(), unit);
}

TermResult<Term> convert_time_unit(ProcessContext &context, const Integer &time, const Term &from, const Term &to) {
    return detail::IntegerAccess::make(context.heap(), convert(time, {unit_rate(from), unit_rate(to)}));
}

// {MegaSecs, Secs, MicroSecs} of a microsecond count.
TermResult<Term> triple(ProcessContext &context, std::int64_t micros) {
    TermFactory factory(context);
    const std::array parts{need(factory.integer(micros / 1'000'000'000'000)),
                           need(factory.integer(micros / 1'000'000 % 1'000'000)),
                           need(factory.integer(micros % 1'000'000))};
    return factory.tuple(parts);
}

TermResult<Term> timestamp(ProcessContext &context) { return triple(context, system_nanos() / 1'000); }

// now/0: system time made strictly increasing across the program.
TermResult<Term> now(ProcessContext &context) {
    static std::atomic<std::int64_t> last{0};
    auto previous = last.load();
    std::int64_t next = 0;
    do {
        next = std::max(system_nanos() / 1'000, previous + 1);
    } while (!last.compare_exchange_weak(previous, next));
    return triple(context, next);
}

// The unique_integer/1 options: positive and monotonic; badarg for others.
void unique_options(const ListArgument &options) {
    for (const auto &option : options.elements) {
        const auto name = option.is_atom() ? need(option.atom_spelling()) : std::string_view{};
        if (name != "positive" && name != "monotonic") {
            bad_argument();
        }
    }
}

// One program-wide counter serves every option set: its values are positive, unique and increasing.
TermResult<Term> unique_integer0(ProcessContext &context) {
    static std::atomic<std::int64_t> counter{0};
    return TermFactory(context).integer(++counter);
}

TermResult<Term> unique_integer1(ProcessContext &context, const ListArgument &options) {
    unique_options(options);
    return unique_integer0(context);
}

// The small integers of a tuple of 3 elements; badarg otherwise.
std::array<std::int64_t, 3> triple_of(const Term &tuple) {
    if (!tuple.is_tuple() || need(tuple.tuple_size()) != 3) {
        bad_argument();
    }
    std::array<std::int64_t, 3> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto value = small(need(tuple.tuple_element(i)).word());
        if (!value) {
            bad_argument();
        }
        values[i] = *value;
    }
    return values;
}

// Whether a time of day is valid.
bool valid_time(const std::array<std::int64_t, 3> &time) {
    return time[0] >= 0 && time[0] <= 23 && time[1] >= 0 && time[1] <= 59 && time[2] >= 0 && time[2] <= 59;
}

// The calendar date of a {Y, M, D} triple (year 0..32767); badarg for an invalid one.
std::chrono::year_month_day calendar_date(const std::array<std::int64_t, 3> &date) {
    if (date[0] < 0 || date[0] > 32767 || date[1] < 1 || date[1] > 12 || date[2] < 1 || date[2] > 31) {
        bad_argument();
    }
    const std::chrono::year_month_day ymd{std::chrono::year(static_cast<int>(date[0])),
                                          std::chrono::month(static_cast<unsigned>(date[1])),
                                          std::chrono::day(static_cast<unsigned>(date[2]))};
    if (!ymd.ok()) {
        bad_argument();
    }
    return ymd;
}

// Seconds since 1970-01-01 00:00:00 of {{Y, M, D}, {H, Mi, S}}, read without a time zone; badarg for an invalid
// date or time.
std::int64_t calendar_seconds(const Term &value) {
    if (!value.is_tuple() || need(value.tuple_size()) != 2) {
        bad_argument();
    }
    const auto date = triple_of(need(value.tuple_element(0)));
    const auto time = triple_of(need(value.tuple_element(1)));
    if (!valid_time(time)) {
        bad_argument();
    }
    const auto day_count = std::chrono::sys_days(calendar_date(date)).time_since_epoch().count();
    return std::int64_t{day_count} * 86'400 + time[0] * 3'600 + time[1] * 60 + time[2];
}

// A tuple of three small integers.
TermResult<Term> three(ProcessContext &context, std::int64_t a, std::int64_t b, std::int64_t c) {
    TermFactory factory(context);
    const std::array parts{need(factory.integer(a)), need(factory.integer(b)), need(factory.integer(c))};
    return factory.tuple(parts);
}

// {{Y, M, D}, {H, Mi, S}} of seconds since 1970, read without a time zone.
TermResult<Term> calendar_term(ProcessContext &context, std::int64_t total) {
    const auto day_count = total >= 0 ? total / 86'400 : -((-total + 86'399) / 86'400);
    const auto rest = total - day_count * 86'400;
    const std::chrono::year_month_day ymd{std::chrono::sys_days(days(day_count))};
    const std::array parts{need(three(context, int{ymd.year()}, unsigned{ymd.month()}, unsigned{ymd.day()})),
                           need(three(context, rest / 3'600, rest / 60 % 60, rest % 60))};
    return TermFactory(context).tuple(parts);
}

// The local broken-down time of seconds since the epoch; badarg when the C library cannot convert them.
std::tm local_tm(std::int64_t total) {
    const auto clock = static_cast<std::time_t>(total);
    std::tm local{};
#ifdef _WIN32
    const bool converted = localtime_s(&local, &clock) == 0;
#else
    const bool converted = localtime_r(&clock, &local) != nullptr;
#endif
    if (!converted) {
        bad_argument();
    }
    return local;
}

// Local calendar seconds of UTC seconds since the epoch.
std::int64_t to_local(std::int64_t total) {
    const auto local = local_tm(total);
    const std::chrono::year_month_day ymd{std::chrono::year(local.tm_year + 1900),
                                          std::chrono::month(static_cast<unsigned>(local.tm_mon + 1)),
                                          std::chrono::day(static_cast<unsigned>(local.tm_mday))};
    const auto day_count = std::chrono::sys_days(ymd).time_since_epoch().count();
    return std::int64_t{day_count} * 86'400 + std::int64_t{local.tm_hour} * 3'600 + std::int64_t{local.tm_min} * 60 +
           local.tm_sec;
}

// Whether a local time is in summer time, as mktime's tm_isdst reads it.
enum class Dst : std::int8_t { unknown = -1, standard = 0, summer = 1 };

// UTC seconds since the epoch of local calendar seconds.
std::int64_t to_universal(std::int64_t local_seconds, Dst dst) {
    const auto day_count = local_seconds >= 0 ? local_seconds / 86'400 : -((-local_seconds + 86'399) / 86'400);
    const auto rest = local_seconds - day_count * 86'400;
    const std::chrono::year_month_day ymd{std::chrono::sys_days(days(day_count))};
    std::tm local{};
    local.tm_year = int{ymd.year()} - 1900;
    local.tm_mon = static_cast<int>(unsigned{ymd.month()}) - 1;
    local.tm_mday = static_cast<int>(unsigned{ymd.day()});
    local.tm_hour = static_cast<int>(rest / 3'600);
    local.tm_min = static_cast<int>(rest / 60 % 60);
    local.tm_sec = static_cast<int>(rest % 60);
    local.tm_isdst = static_cast<int>(dst);
    const auto clock = std::mktime(&local);
    if (clock == static_cast<std::time_t>(-1)) {
        bad_argument();
    }
    return clock;
}

std::int64_t system_seconds() {
    return duration_cast<seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

TermResult<Term> universaltime(ProcessContext &context) { return calendar_term(context, system_seconds()); }

TermResult<Term> localtime(ProcessContext &context) { return calendar_term(context, to_local(system_seconds())); }

// date/0 and time/0: the parts of localtime/0.
TermResult<Term> date(ProcessContext &context) { return need(localtime(context)).tuple_element(0); }

TermResult<Term> time(ProcessContext &context) { return need(localtime(context)).tuple_element(1); }

TermResult<Term> universaltime_to_localtime(ProcessContext &context, const Term &value) {
    return calendar_term(context, to_local(calendar_seconds(value)));
}

TermResult<Term> localtime_to_universaltime1(ProcessContext &context, const Term &value) {
    return calendar_term(context, to_universal(calendar_seconds(value), Dst::unknown));
}

// localtime_to_universaltime(DateTime, IsDst): true, false or undefined.
TermResult<Term> localtime_to_universaltime2(ProcessContext &context, const Term &value, const AtomArgument &dst) {
    auto flag = Dst::unknown;
    if (dst.spelling == "true") {
        flag = Dst::summer;
    } else if (dst.spelling == "false") {
        flag = Dst::standard;
    } else if (dst.spelling != "undefined") {
        bad_argument();
    }
    return calendar_term(context, to_universal(calendar_seconds(value), flag));
}

TermResult<Term> posixtime_to_universaltime(ProcessContext &context, std::int64_t posix) {
    return calendar_term(context, posix);
}

TermResult<Term> universaltime_to_posixtime(ProcessContext &context, const Term &value) {
    return TermFactory(context).integer(calendar_seconds(value));
}

constexpr std::array TIME_BUILTINS{
    typed_entry<monotonic_time0>("erlang", "monotonic_time"),
    typed_entry<monotonic_time1>("erlang", "monotonic_time"),
    typed_entry<system_time0>("erlang", "system_time"),
    typed_entry<system_time1>("erlang", "system_time"),
    typed_entry<time_offset0>("erlang", "time_offset"),
    typed_entry<time_offset1>("erlang", "time_offset"),
    typed_entry<convert_time_unit>("erlang", "convert_time_unit"),
    typed_entry<timestamp>("erlang", "timestamp"),
    typed_entry<now>("erlang", "now"),
    typed_entry<unique_integer0>("erlang", "unique_integer"),
    typed_entry<unique_integer1>("erlang", "unique_integer"),
    typed_entry<universaltime>("erlang", "universaltime"),
    typed_entry<localtime>("erlang", "localtime"),
    typed_entry<date>("erlang", "date"),
    typed_entry<time>("erlang", "time"),
    typed_entry<universaltime_to_localtime>("erlang", "universaltime_to_localtime"),
    typed_entry<localtime_to_universaltime1>("erlang", "localtime_to_universaltime"),
    typed_entry<localtime_to_universaltime2>("erlang", "localtime_to_universaltime"),
    typed_entry<posixtime_to_universaltime>("erlang", "posixtime_to_universaltime"),
    typed_entry<universaltime_to_posixtime>("erlang", "universaltime_to_posixtime"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> time_builtins() noexcept { return builtins::TIME_BUILTINS; }
} // namespace clause::runtime
