#include "../terms/integers.hpp"
#include "collections/collections.hpp"
#include <array>

// The calendar builtins (plan 21 step LIB5, docs/builtins.md#calendar): the proleptic Gregorian arithmetic of
// calendar, over integers of any size with Erlang's truncating div and rem. A shape no clause of OTP's accepts is
// function_clause, a non-integer where OTP computes with it badarith; dates out of range are computed as OTP
// computes them.
namespace clause::runtime::builtins::calendar {
namespace {
using collections::no_clause;
using Integer = detail::Integer;

constexpr std::int64_t SECONDS_PER_DAY = 86400;
constexpr std::int64_t DAYS_PER_ERA = 146097;
constexpr std::int64_t MARCH_1_YEAR_0 = 60;

// The roles of calendar arguments.
using Year = Named<struct YearRole>;
using Month = Named<struct MonthRole>;
using Day = Named<struct DayRole>;
using Moment = Named<struct MomentRole>;
using Other = Named<struct OtherRole>;

[[noreturn]] void badarith() { throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith}; }

// An integer OTP computes with: badarith for anything else.
Integer arith(const Term &term) {
    if (!term.is_integer()) {
        badarith();
    }
    return need(detail::integer_read(term));
}

// The non-negative remainder of calendar's mod/2.
Integer modulo(const Integer &n, std::int64_t d) {
    Integer r = n % d;
    return r < 0 ? Integer(r + d) : r;
}

Term integer_term(ProcessContext &context, const Integer &value) {
    return need(detail::IntegerAccess::make(context.heap(), value));
}

Term tuple(ProcessContext &context, std::span<const Term> parts) { return need(TermFactory(context).tuple(parts)); }

// The parts of a tuple of `size`; function_clause for anything else.
std::vector<Term> parts_of(const Term &term, std::size_t size) {
    if (!term.is_tuple() || need(term.tuple_size()) != size) {
        no_clause();
    }
    return need(term.tuple_elements());
}

// A civil date's fields.
struct Civil {
    Integer year;
    Integer month;
    Integer day;
};

// The days from 0000-01-01 of a civil date, by OTP's era formula.
Integer civil_days(const Civil &date) {
    const auto &[year, month, day] = date;
    const Integer y = year - (month <= 2 ? 1 : 0);
    const Integer era = y >= 0 ? Integer(y / 400) : Integer((y - 399) / 400);
    const Integer year_of_era = y - era * 400;
    const Integer month_prime = month > 2 ? Integer(month - 3) : Integer(month + 9);
    const Integer day_of_year = (153 * month_prime + 2) / 5 + day - 1;
    const Integer day_of_era = 365 * year_of_era + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * DAYS_PER_ERA + day_of_era + MARCH_1_YEAR_0;
}

// The days of date_to_gregorian_days/3's arguments: function_clause unless Day is a positive integer, badarith
// unless Year and Month are integers.
Integer days_of(const Term &year, const Term &month, const Term &day) {
    if (!day.is_integer() || arith(day) <= 0) {
        no_clause();
    }
    const auto m = arith(month);
    return civil_days({arith(year), m, arith(day)});
}

Integer days_of_date(const Term &date) {
    const auto parts = parts_of(date, 3);
    return days_of(parts[0], parts[1], parts[2]);
}

// The civil date of a number of days from 0000-01-01.
std::array<Integer, 3> date_of(const Integer &days) {
    const Integer z = days - MARCH_1_YEAR_0;
    const Integer era = z >= 0 ? Integer(z / DAYS_PER_ERA) : Integer((z - DAYS_PER_ERA + 1) / DAYS_PER_ERA);
    const Integer day_of_era = z - era * DAYS_PER_ERA;
    const Integer year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / (DAYS_PER_ERA - 1)) / 365;
    const Integer day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const Integer month_prime = (5 * day_of_year + 2) / 153;
    const Integer day = day_of_year - (153 * month_prime + 2) / 5 + 1;
    const Integer month = month_prime < 10 ? Integer(month_prime + 3) : Integer(month_prime - 9);
    const Integer year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);
    return {year, month, day};
}

Term date_term(ProcessContext &context, const std::array<Integer, 3> &date) {
    return tuple(context, std::array{integer_term(context, date[0]), integer_term(context, date[1]),
                                     integer_term(context, date[2])});
}

// A time field in 0..high, or none.
std::optional<std::int64_t> field(const Term &term, std::int64_t high) {
    const auto value = small(term.word());
    return value && *value >= 0 && *value <= high ? value : std::nullopt;
}

// The seconds of a time of day; function_clause unless {H, M, S} of a valid time.
Integer seconds_of_time(const Term &time) {
    const auto parts = parts_of(time, 3);
    const auto h = field(parts[0], 23);
    const auto m = field(parts[1], 59);
    const auto s = field(parts[2], 59);
    if (!h || !m || !s) {
        no_clause();
    }
    return Integer(*h * 3600 + *m * 60 + *s);
}

// {H, M, S} of seconds in 0..86399.
Term time_term(ProcessContext &context, std::int64_t seconds) {
    const std::array parts{integer_term(context, seconds / 3600), integer_term(context, seconds % 3600 / 60),
                           integer_term(context, seconds % 60)};
    return tuple(context, parts);
}

// {Days, Seconds of the day} of a number of seconds, the seconds non-negative.
std::pair<Integer, std::int64_t> days_and_seconds(const Integer &seconds) {
    Integer days = seconds / SECONDS_PER_DAY;
    Integer rest = seconds % SECONDS_PER_DAY;
    if (rest < 0) {
        days -= 1;
        rest += SECONDS_PER_DAY;
    }
    return {days, rest.convert_to<std::int64_t>()};
}

Term days_and_time(ProcessContext &context, const Integer &seconds) {
    const auto [days, rest] = days_and_seconds(seconds);
    return tuple(context, std::array{integer_term(context, days), time_term(context, rest)});
}

// date_to_gregorian_days(Year, Month, Day) and date_to_gregorian_days(Date).
Term date_to_days3(ProcessContext &context, const Year &year, const Month &month, const Day &day) {
    return integer_term(context, days_of(year.term, month.term, day.term));
}

Term date_to_days1(ProcessContext &context, const Moment &date) {
    return integer_term(context, days_of_date(date.term));
}

// gregorian_days_to_date(Days).
Term days_to_date(ProcessContext &context, const Moment &days) { return date_term(context, date_of(arith(days.term))); }

// The seconds from 0000-01-01 00:00:00 of {Date, Time}.
Integer seconds_of_datetime(const Term &datetime) {
    const auto parts = parts_of(datetime, 2);
    const auto days = days_of_date(parts[0]);
    return SECONDS_PER_DAY * days + seconds_of_time(parts[1]);
}

// datetime_to_gregorian_seconds({Date, Time}) and gregorian_seconds_to_datetime(Secs).
Term datetime_to_seconds(ProcessContext &context, const Moment &datetime) {
    return integer_term(context, seconds_of_datetime(datetime.term));
}

Term seconds_to_datetime(ProcessContext &context, const Moment &seconds) {
    const auto [days, rest] = days_and_seconds(arith(seconds.term));
    return tuple(context, std::array{date_term(context, date_of(days)), time_term(context, rest)});
}

// 1 (Monday) to 7 of a number of days.
Integer weekday(const Integer &days) { return modulo(days + 5, 7) + 1; }

// day_of_the_week(Year, Month, Day) and day_of_the_week(Date).
Term day_of_week3(ProcessContext &context, const Year &year, const Month &month, const Day &day) {
    return integer_term(context, weekday(days_of(year.term, month.term, day.term)));
}

Term day_of_week1(ProcessContext &context, const Moment &date) {
    return integer_term(context, weekday(days_of_date(date.term)));
}

bool leap(const Integer &year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

// is_leap_year(Year): function_clause unless an integer.
Term is_leap_year(ProcessContext &context, const Year &year) {
    if (!year.term.is_integer()) {
        no_clause();
    }
    return need(TermFactory(context).boolean(leap(arith(year.term))));
}

// The days of a month: function_clause unless Year is an integer and Month 1..12.
std::int64_t month_length(const Year &year, const Month &month) {
    const auto m = field(month.term, 12);
    if (!year.term.is_integer() || !m || *m == 0) {
        no_clause();
    }
    if (*m == 2) {
        return leap(arith(year.term)) ? 29 : 28;
    }
    return *m == 4 || *m == 6 || *m == 9 || *m == 11 ? 30 : 31;
}

Term last_day_of_the_month(ProcessContext &context, const Year &year, const Month &month) {
    return integer_term(context, month_length(year, month));
}

// valid_date(Year, Month, Day) and valid_date(Date): function_clause unless integers.
Term valid_date3(ProcessContext &context, const Year &year, const Month &month, const Day &day) {
    if (!year.term.is_integer() || !month.term.is_integer() || !day.term.is_integer()) {
        no_clause();
    }
    const auto m = arith(month.term);
    const auto d = arith(day.term);
    const auto valid = m > 0 && m < 13 && d > 0 && d <= month_length(year, month);
    return need(TermFactory(context).boolean(valid));
}

Term valid_date1(ProcessContext &context, const Moment &date) {
    const auto parts = parts_of(date.term, 3);
    return valid_date3(context, {parts[0]}, {parts[1]}, {parts[2]});
}

// seconds_to_time(Secs): function_clause unless 0 =< Secs < 86400; a float in range is badarith (OTP's rem).
Term seconds_to_time(ProcessContext &context, const Moment &seconds) {
    const auto &term = seconds.term;
    const auto value = term.is_float() ? need(term.float_value()) : 0.0;
    const auto in_float_range = term.is_float() && value >= 0.0 && value < SECONDS_PER_DAY;
    const auto integer = small(term.word());
    if (in_float_range) {
        badarith();
    }
    if (!integer || *integer < 0 || *integer >= SECONDS_PER_DAY) {
        no_clause();
    }
    return time_term(context, *integer);
}

// seconds_to_daystime(Secs): {Days, Time}.
Term seconds_to_daystime(ProcessContext &context, const Moment &seconds) {
    return days_and_time(context, arith(seconds.term));
}

// time_to_seconds(Time).
Term time_to_seconds(ProcessContext &context, const Moment &time) {
    return integer_term(context, seconds_of_time(time.term));
}

// time_difference(T1, T2): {Days, Time} from T1 to T2; function_clause unless both are datetimes.
Term time_difference(ProcessContext &context, const Moment &first, const Other &second) {
    for (const auto &datetime : {first.term, second.term}) {
        const auto parts = parts_of(datetime, 2);
        parts_of(parts[0], 3);
        parts_of(parts[1], 3);
    }
    const auto later = seconds_of_datetime(second.term);
    return days_and_time(context, later - seconds_of_datetime(first.term));
}

// The days of Monday of ISO week 1 of `year`.
Integer week_one(const Integer &year) {
    const auto first = civil_days({year, 1, 1});
    const auto day = weekday(first);
    return day <= 4 ? Integer(first - day + 1) : Integer(first + 7 - day + 1);
}

// iso_week_number(Date): {Year, Week} of the ISO week of a date.
Term iso_week_number(ProcessContext &context, const Moment &date) {
    const auto days = days_of_date(date.term);
    const auto year = arith(need(date.term.tuple_element(0)));
    const auto start = week_one(year);
    const auto next = week_one(year + 1);
    if (start <= days && days < next) {
        return tuple(context, std::array{integer_term(context, year), integer_term(context, (days - start) / 7 + 1)});
    }
    if (days >= next) {
        return tuple(context, std::array{integer_term(context, year + 1), integer_term(context, 1)});
    }
    const auto long_year = weekday(civil_days({year - 1, 1, 1})) == 4 || weekday(civil_days({year - 1, 12, 31})) == 4;
    return tuple(context, std::array{integer_term(context, year - 1), integer_term(context, long_year ? 53 : 52)});
}

constexpr std::array CALENDAR_BUILTINS{
    typed_entry<date_to_days3>("calendar", "date_to_gregorian_days"),
    typed_entry<date_to_days1>("calendar", "date_to_gregorian_days"),
    typed_entry<days_to_date>("calendar", "gregorian_days_to_date"),
    typed_entry<datetime_to_seconds>("calendar", "datetime_to_gregorian_seconds"),
    typed_entry<seconds_to_datetime>("calendar", "gregorian_seconds_to_datetime"),
    typed_entry<day_of_week3>("calendar", "day_of_the_week"),
    typed_entry<day_of_week1>("calendar", "day_of_the_week"),
    typed_entry<is_leap_year>("calendar", "is_leap_year"),
    typed_entry<last_day_of_the_month>("calendar", "last_day_of_the_month"),
    typed_entry<valid_date3>("calendar", "valid_date"),
    typed_entry<valid_date1>("calendar", "valid_date"),
    typed_entry<seconds_to_time>("calendar", "seconds_to_time"),
    typed_entry<seconds_to_daystime>("calendar", "seconds_to_daystime"),
    typed_entry<time_to_seconds>("calendar", "time_to_seconds"),
    typed_entry<time_difference>("calendar", "time_difference"),
    typed_entry<iso_week_number>("calendar", "iso_week_number"),
};
} // namespace
} // namespace clause::runtime::builtins::calendar

namespace clause::runtime {
std::span<const BuiltinEntry> calendar_builtins() noexcept { return builtins::calendar::CALENDAR_BUILTINS; }
} // namespace clause::runtime
